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
#ifndef SCHACH_THEMES_H
#define SCHACH_THEMES_H

// assets/items/themes.json: what a Lichess puzzle theme means for this app.
//
// The same file is read by tools/import_lichess_puzzles.py, which builds the
// shipped bank on a build host, and by PuzzleFeed, which turns the puzzles the
// Lichess API hands the phone into the same items. Two copies of these tables
// would drift apart without anybody noticing, and the two halves of one bank
// would then measure different things.
//
// A missing or broken file is a stated condition, not a silent default: the
// feed then fetches nothing and says why. The shipped bank does not need it.

#include "core/Skill.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace schach {

class Themes
{
public:
    bool load(const QString& path);
    bool isEmpty() const { return m_order.isEmpty(); }
    const QString& error() const { return m_error; }

    // teacher.md §3.1. `TAK` is what is left when no other dimension claims it.
    core::Dimension dimensionOf(const QSet<QString>& themes) const;
    // §6.3's three sorts: "quiet", "defensive", "other".
    QString sortOf(const QSet<QString>& themes) const;
    // One German sentence, shown *after* solving (§6.2), empty when no theme
    // of this puzzle has one. PuzzleFeed then computes one from the board.
    QString sentenceFor(const QSet<QString>& themes) const;

private:
    struct Entry {
        QString key;
        QSet<QString> themes;
    };

    QStringList m_order;              // the dimensions, in the order they win
    QVector<Entry> m_byDimension;
    QString m_default;
    QSet<QString> m_motifs;
    QVector<Entry> m_bySort;
    QVector<QPair<QString, QString> > m_sentences;
    QString m_error;
};

} // namespace schach

#endif // SCHACH_THEMES_H
