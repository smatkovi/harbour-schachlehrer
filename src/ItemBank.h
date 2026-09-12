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

#include <QString>
#include <QStringList>
#include <QVector>

namespace schach {

// One item of the placement test: a position, the move that solves it, and the
// difficulty at which it was calibrated (teacher.md §4.2). The bank is data,
// not code, and every item was verified against the engine before it was
// written — see tools/verify_items.py.
struct PlacementItem {
    QString id;
    QString fen;
    QString solution;        // UCI, the one move that counts
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
    bool load(const QString& path);
    bool isEmpty() const { return m_items.isEmpty(); }
    int count() const { return m_items.size(); }
    const QString& error() const { return m_error; }

    // The unused item of that dimension whose difficulty is closest to the
    // target; falls back to any dimension before giving up (§4.5).
    const PlacementItem* pick(core::Dimension dimension, double difficulty,
                              const QStringList& alreadyUsed) const;

private:
    QVector<PlacementItem> m_items;
    QString m_error;
};

} // namespace schach

#endif // ITEMBANK_H
