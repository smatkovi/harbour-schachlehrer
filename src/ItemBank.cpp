/*
    Copyright (C) 2026 smatkovi

    This file is part of harbour-schachlehrer.

    harbour-schachlehrer is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    harbour-schachlehrer is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with harbour-schachlehrer. If not, see <https://www.gnu.org/licenses/>.

    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "ItemBank.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace schach {

namespace {
// How far from the asked-for difficulty an item may sit and still count as a
// fit. teacher.md §4.5 asks for p* = 0.70, i.e. delta = theta - 147; +-120 Elo
// around that is p between 0.54 and 0.82, which is the band the spec is
// willing to pay for (§4.5 prices the loss of information explicitly).
const double kWindowElo = 120.0;
} // namespace

ItemBank::ItemBank()
    : m_random(static_cast<unsigned int>(QDateTime::currentMSecsSinceEpoch()))
{
}

void ItemBank::setSeed(unsigned int seed)
{
    m_random.seed(seed);
}

bool ItemBank::load(const QString& path)
{
    m_items.clear();
    m_ids.clear();
    m_easiest = 0.0;
    m_hardest = 0.0;
    return merge(path);
}

bool ItemBank::merge(const QString& path)
{
    m_error.clear();
    const int before = m_items.size();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull()) {
        m_error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
        return false;
    }
    const QJsonArray array = document.isArray() ? document.array()
                                                : document.object().value(
                                                      QStringLiteral("items")).toArray();
    for (int index = 0; index < array.size(); ++index) {
        const QJsonObject object = array.at(index).toObject();
        PlacementItem item;
        item.id = object.value(QStringLiteral("id")).toString();
        item.fen = object.value(QStringLiteral("fen")).toString();
        item.solution = object.value(QStringLiteral("solution")).toString();
        // Two spellings, because the bank grew a second one: "solution" is a
        // single move, "line" is the whole sequence. Either may be given; the
        // other is derived, so neither half of the bank has to be rewritten.
        const QJsonArray lineArray = object.value(QStringLiteral("line")).toArray();
        for (int i = 0; i < lineArray.size(); ++i) {
            const QString move = lineArray.at(i).toString();
            if (!move.isEmpty())
                item.line << move;
        }
        if (item.line.isEmpty() && !item.solution.isEmpty())
            item.line << item.solution;
        else if (item.solution.isEmpty() && !item.line.isEmpty())
            item.solution = item.line.first();
        const QJsonArray accepted = object.value(QStringLiteral("alsoAccepted")).toArray();
        for (int i = 0; i < accepted.size(); ++i)
            item.alsoAccepted << accepted.at(i).toString();
        item.dimension = core::dimensionFromKey(
            object.value(QStringLiteral("dimension")).toString().toStdString());
        item.difficulty = object.value(QStringLiteral("difficulty")).toDouble(1200.0);
        item.explanation = object.value(QStringLiteral("explanation")).toString();
        // An item without a position or a solution is worse than no item.
        if (item.id.isEmpty() || item.fen.isEmpty() || item.solution.isEmpty())
            continue;
        if (m_ids.contains(item.id))
            continue;
        m_ids.insert(item.id);
        if (m_items.isEmpty() || item.difficulty < m_easiest)
            m_easiest = item.difficulty;
        if (m_items.isEmpty() || item.difficulty > m_hardest)
            m_hardest = item.difficulty;
        m_items.append(item);
    }
    if (m_items.size() == before && m_error.isEmpty())
        m_error = QStringLiteral("%1: no usable items").arg(path);
    return m_items.size() > before;
}


double ItemBank::difficultyOf(const QString& id) const
{
    for (int index = 0; index < m_items.size(); ++index) {
        if (m_items.at(index).id == id)
            return m_items.at(index).difficulty;
    }
    return 0.0;
}

int ItemBank::countNear(core::Dimension dimension, double difficulty, double window) const
{
    int found = 0;
    for (int index = 0; index < m_items.size(); ++index) {
        const PlacementItem& item = m_items.at(index);
        if (item.dimension == dimension && std::fabs(item.difficulty - difficulty) <= window)
            ++found;
    }
    return found;
}

QStringList ItemBank::thinDimensions(double difficulty, double window) const
{
    QStringList out;
    for (int i = 0; i < core::kDimensionCount; ++i) {
        const core::Dimension dimension = static_cast<core::Dimension>(i);
        if (countNear(dimension, difficulty, window) < 2)
            out << QString::fromLatin1(core::dimensionKey(dimension));
    }
    return out;
}

const PlacementItem* ItemBank::pick(core::Dimension dimension, double difficulty,
                                    const QStringList& alreadyUsed,
                                    const QStringList& seenBefore) const
{
    // `difficulty` is adjusted below, so it is taken by value on purpose.
    // A target outside what the bank holds is clamped into it. Without this a
    // learner who answers everything wrong walks past the easiest item the
    // bank has, every window after that is empty, and the fallback hands out
    // the same position to everyone — which is the defect this whole function
    // was rewritten for, one level further down.
    if (!m_items.isEmpty())
        difficulty = std::min(std::max(difficulty, m_easiest), m_hardest);

    // Lookups, not scans: with the imported bank this runs over thousands of
    // items, and `seenBefore` grows with every test the learner ever took.
    // Filled by hand — QList::toSet() is deprecated in Qt 5.14 and the
    // iterator-pair constructor it points at does not exist in 5.6.
    QSet<QString> used;
    for (int i = 0; i < alreadyUsed.size(); ++i)
        used.insert(alreadyUsed.at(i));
    QSet<QString> seen;
    for (int i = 0; i < seenBefore.size(); ++i)
        seen.insert(seenBefore.at(i));

    // Four rounds of admission, each looser than the last, and a round is only
    // entered when the tighter one found nothing: the asked-for dimension with
    // items the learner has never seen, the same dimension with items from
    // earlier tests allowed, then the same two over every dimension. The test
    // keeps running when one dimension runs out, which is what the two passes
    // did before — it just no longer reaches for a stale or a much harder item
    // while a fitting one is still on the shelf.
    //
    // A fitting item always beats a better-matching-but-distant one, so the
    // below/above fallbacks are remembered across the rounds and only used
    // when no round found anything inside the window at all. They keep their
    // round order, which is what stops a learner below the bank's floor from
    // being handed the same easiest item on every test.
    const PlacementItem* firstBelow = 0;
    const PlacementItem* firstAbove = 0;

    for (int round = 0; round < 4; ++round) {
        const bool anyDimension = round >= 2;
        const bool allowSeen = (round % 2) == 1;

        QVector<const PlacementItem*> fitting;   // inside the window
        const PlacementItem* below = 0;          // nearest at or below target
        const PlacementItem* above = 0;          // nearest above, the last resort

        for (int index = 0; index < m_items.size(); ++index) {
            const PlacementItem& item = m_items.at(index);
            if (!anyDimension && item.dimension != dimension)
                continue;
            if (used.contains(item.id))
                continue;
            if (!allowSeen && seen.contains(item.id))
                continue;

            const double distance = item.difficulty - difficulty;
            if (std::fabs(distance) <= kWindowElo) {
                fitting.append(&item);
            } else if (distance < 0.0) {
                if (!below || item.difficulty > below->difficulty)
                    below = &item;
            } else {
                if (!above || item.difficulty < above->difficulty)
                    above = &item;
            }
        }

        // Inside the window every item is as good a measurement as the next,
        // so the choice is a draw and not an order. This is what makes two
        // placement tests differ.
        if (!fitting.isEmpty()) {
            std::uniform_int_distribution<int> draw(0, fitting.size() - 1);
            return fitting.at(draw(m_random));
        }
        if (!firstBelow)
            firstBelow = below;
        if (!firstAbove)
            firstAbove = above;
    }

    // Nothing fits anywhere. An item that is too easy costs information; an
    // item that is far too hard costs the learner. §6.1 prices both, and the
    // easy one is the cheap mistake — so `below` wins, and `above` is reached
    // only when the bank holds nothing easier at all.
    if (firstBelow)
        return firstBelow;
    if (firstAbove)
        return firstAbove;
    return 0;
}

} // namespace schach
