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

// Turning whatever TeacherEngine puts into `squares` into one spelling.
// A singleton so that the board, the piece and the promotion picker all agree
// without any of them owning the rule.
QtObject {

    // "" for an empty or unreadable square, otherwise two lower-case letters:
    // colour ("w"/"b") then type ("k","q","r","b","n","p").
    function normalise(value) {
        if (value === undefined || value === null)
            return ""

        // A square may arrive as a plain string or as a map; take the obvious
        // key out of the map and carry on with the string.
        if (typeof value === "object") {
            if (value.piece !== undefined)
                value = value.piece
            else if (value.code !== undefined)
                value = value.code
            else if (value.symbol !== undefined)
                value = value.symbol
            else
                return ""
        }

        var s = ("" + value).replace(/^\s+|\s+$/g, "")
        if (s === "" || s === "." || s === "-" || s === "0")
            return ""

        if (s.length === 1) {
            // FEN: upper case is White.
            var lower = s.toLowerCase()
            if ("kqrbnp".indexOf(lower) < 0)
                return ""
            return (s === lower ? "b" : "w") + lower
        }

        var c = s.charAt(0).toLowerCase()
        var t = s.charAt(1).toLowerCase()
        if ((c === "w" || c === "b") && "kqrbnp".indexOf(t) >= 0)
            return c + t
        return ""
    }

    function isPawn(value) {
        return normalise(value).charAt(1) === "p"
    }
}
