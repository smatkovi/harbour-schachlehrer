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

// `moveList` is a QVariantList whose element shape docs/design.md §4 does not
// fix. It may be a list of SAN strings or a list of maps; the board page, the
// analysis page and the cover all need the same reading of it, so it is read
// in one place.
QtObject {

    // The move itself, in whatever notation TeacherEngine produced.
    function san(entry) {
        if (entry === undefined || entry === null)
            return ""
        if (typeof entry === "object") {
            if (entry.san !== undefined) return "" + entry.san
            if (entry.move !== undefined) return "" + entry.move
            if (entry.text !== undefined) return "" + entry.text
            if (entry.uci !== undefined) return "" + entry.uci
            return ""
        }
        return "" + entry
    }

    // A sentence about this move, if the analysis produced one. Never a number.
    function comment(entry) {
        if (entry === null || typeof entry !== "object")
            return ""
        if (entry.comment !== undefined) return "" + entry.comment
        if (entry.text !== undefined && entry.san !== undefined) return "" + entry.text
        return ""
    }

    // "1. e4 e5  2. Sf3 Sc6 …" for the running board page, where a full move
    // list would take the board's room.
    function asText(list) {
        if (!list || list.length === 0)
            return ""
        var out = []
        for (var i = 0; i < list.length; ++i) {
            if (i % 2 === 0)
                out.push((i / 2 + 1) + ".")
            out.push(san(list[i]))
        }
        return out.join(" ")
    }

    // The ply a finding belongs to, or -1. Findings carry a FEN and the move
    // that was played (docs/design.md §3); if they also carry a ply the
    // analysis page can put the sentence under the right move.
    function plyOf(finding) {
        if (finding === null || typeof finding !== "object")
            return -1
        if (finding.ply !== undefined) return Number(finding.ply)
        if (finding.moveIndex !== undefined) return Number(finding.moveIndex)
        if (finding.halfMove !== undefined) return Number(finding.halfMove)
        return -1
    }
}
