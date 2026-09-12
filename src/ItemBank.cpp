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
#include <QtGlobal>

#include <cmath>

namespace schach {

bool ItemBank::load(const QString& path)
{
    m_items.clear();
    m_error.clear();

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
        m_items.append(item);
    }
    if (m_items.isEmpty() && m_error.isEmpty())
        m_error = QStringLiteral("%1: no usable items").arg(path);
    return !m_items.isEmpty();
}

const PlacementItem* ItemBank::pick(core::Dimension dimension, double difficulty,
                                    const QStringList& alreadyUsed) const
{
    const PlacementItem* best = 0;
    double bestDistance = 0.0;
    // Two passes: the asked-for dimension first, any dimension second, so the
    // test keeps running even when one dimension runs out of items.
    for (int pass = 0; pass < 2 && !best; ++pass) {
        for (int index = 0; index < m_items.size(); ++index) {
            const PlacementItem& item = m_items.at(index);
            if (pass == 0 && item.dimension != dimension)
                continue;
            if (alreadyUsed.contains(item.id))
                continue;
            const double distance = std::fabs(item.difficulty - difficulty);
            if (!best || distance < bestDistance) {
                best = &item;
                bestDistance = distance;
            }
        }
    }
    return best;
}

} // namespace schach
