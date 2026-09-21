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

import QtQuick 1.1

// The choices that belong to the presentation, not to the teaching. Everything
// the method owns — learning level, schedule, sparring handicap — is a matter
// for TeacherEngine and its database (docs/design.md §4, §6); only what the
// screen does with the answers lives here.
//
// A singleton cannot read the root context, so the first page that comes up
// hands the engine in through adopt().
QtObject {
    id: prefs

    // "Keine Bewertungszahl ohne Satz" (docs/design.md §4). The sentence is
    // never optional; the number is, and at the lower learning levels it is
    // off, because a centipawn value teaches a beginner nothing and invites
    // exactly the wrong kind of attention.
    property bool showNumbers: false

    // File letters and rank digits along the edge of the board.
    property bool showCoordinates: true

    // Mark the squares the selected piece may move to.
    property bool showLegalTargets: true

    // Confirm a move with a second tap on the target square instead of playing
    // it straight away. Off by default: the second tap is the target tap.
    property bool markLastMove: true

    property bool adopted: false

    function adopt(engine) {
        if (prefs.adopted || !engine)
            return
        prefs.adopted = true
    }
}
