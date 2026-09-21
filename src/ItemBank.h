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
#ifndef ITEMBANK_H
#define ITEMBANK_H

#include "core/Skill.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <random>

namespace schach {

// One item of the placement test: a position, the move that solves it, and the
// difficulty at which it was calibrated (teacher.md §4.2). The bank is data,
// not code, and every item was verified against the engine before it was
// written — see tools/verify_items.py.
struct PlacementItem {
    QString id;
    QString fen;
    QString solution;        // UCI, the learner's first move — always line.first()
    // The whole line, UCI, learner first and alternating (teacher.md §6.5).
    // A one-move item has one entry; nothing in the app distinguishes the two
    // cases except how many moves have to be entered.
    QStringList line;
    QStringList alsoAccepted;// equally good moves, if the engine found any
    core::Dimension dimension = core::Dimension::TAK;
    double difficulty = 1200.0;
    QString explanation;     // one German sentence, shown after answering
};

// Loads the bank once and hands out items. An empty bank is a stated
// condition, never a silent fallback to the starting position.
class ItemBank
{
public:
    // A default-constructed std::mt19937 always produces the same sequence,
    // which is the bug this class was just cured of. It is seeded here.
    ItemBank();

    // Replaces whatever was loaded before.
    bool load(const QString& path);
    // Adds to it. The hand-written bank and the imported Lichess one are two
    // files with two different jobs: the first has German explanations and
    // carries §6.3's quiet quota, the second has the spread and the calibrated
    // difficulties (§4.2). Duplicate ids are ignored, so loading a file twice
    // is harmless.
    bool merge(const QString& path);
    bool isEmpty() const { return m_items.isEmpty(); }
    int count() const { return m_items.size(); }
    const QString& error() const { return m_error; }

    // One item of that dimension near `difficulty`, drawn at random from the
    // ones that fit (§4.5). Three properties matter and none of them is
    // optional:
    //
    //  * **Random, not nearest.** Picking the nearest item makes the test
    //    deterministic: the same six positions opened every placement test,
    //    every time, because theta always started at the same place. A
    //    learner who remembers a position is not being measured (§5.2).
    //  * **Never much harder than asked.** When nothing close is left, the
    //    item below the target wins over the item above it. Before this the
    //    picker answered a run of wrong answers with 1223 → 1342 → 1479 →
    //    1684, which is the exact condition under which retrieval practice
    //    is worth nothing (§6.1, Rowland: g = 0.03 below 50 % correct).
    //  * **`seenBefore` is soft.** Items from earlier tests are avoided while
    //    unseen ones exist and used again when the bank runs dry, because an
    //    old item still measures better than no item.
    const PlacementItem* pick(core::Dimension dimension, double difficulty,
                              const QStringList& alreadyUsed,
                              const QStringList& seenBefore = QStringList()) const;

    // The tests need the same draw twice; the app wants a different one every
    // time and seeds itself from the clock.
    void setSeed(unsigned int seed);

    // How many items of that dimension sit within `window` Elo of `difficulty`.
    // The draw can only be a draw where there is something to draw from: with
    // fewer than two, that question is the same position for every learner on
    // every test, however good the picker is. Used by the tests and by
    // `thinDimensions()` to say so out loud instead of pretending.
    int countNear(core::Dimension dimension, double difficulty, double window = 120.0) const;
    // What the bank can actually serve. A request outside this is clamped into
    // it by pick(): asking for something that does not exist is not a harder
    // question, it is no question.
    double easiest() const { return m_easiest; }
    double hardest() const { return m_hardest; }
    // The difficulty of one item by id, or 0 when it is not in the bank.
    double difficultyOf(const QString& id) const;
    // The dimensions that cannot offer a choice at `difficulty`, as keys.
    QStringList thinDimensions(double difficulty, double window = 120.0) const;

private:
    QVector<PlacementItem> m_items;
    // Ids already in the bank. A linear scan per merged item would be
    // quadratic, and the imported bank has thousands of them.
    QSet<QString> m_ids;
    double m_easiest = 0.0;
    double m_hardest = 0.0;
    QString m_error;
    mutable std::mt19937 m_random;
};

} // namespace schach

#endif // ITEMBANK_H
