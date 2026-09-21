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

// The six skill dimensions of chess-spec/teacher.md §3.1, and the three-step
// ordinal verdict of §4.6 — the placement test is honest about not producing
// numbers per dimension, so the screen does not invent any.
QtObject {
    id: dims

    property variant keys: ["TAK", "SRG", "REC", "END", "STL", "ERD"]

    function nameFor(key) {
        switch (("" + key).toUpperCase()) {
        case "TAK": return qsTr("Taktik")
        case "SRG": return qsTr("Sorgfalt")
        case "REC": return qsTr("Rechnen")
        case "END": return qsTr("Endspiel")
        case "STL": return qsTr("Stellungsurteil")
        case "ERD": return qsTr("Eröffnung")
        }
        return "" + key
    }

    // The one question each dimension answers (teacher.md §3.1).
    function questionFor(key) {
        switch (("" + key).toUpperCase()) {
        case "TAK": return qsTr("Erkennst du ein Motiv, wenn es da ist?")
        case "SRG": return qsTr("Prüfst du vor dem Zug, ob etwas hängt oder droht?")
        case "REC": return qsTr("Kannst du eine Folge korrekt bis zum Ende rechnen?")
        case "END": return qsTr("Verwertest du, was du erreicht hast?")
        case "STL": return qsTr("Weißt du, was in einer ruhigen Stellung zu tun ist?")
        case "ERD": return qsTr("Kommst du ohne Schaden aus den ersten zwölf Zügen?")
        }
        return ""
    }

    // teacher.md §4.6: auffällig schwach / unauffällig / auffällig stark.
    // Anything the engine has not decided on stays "unauffällig", which is the
    // truthful answer when four items per dimension are all there is.
    function bandName(band) {
        var b = ("" + band).toLowerCase()
        if (b === "weak" || b === "low" || b === "schwach" || b === "-1")
            return qsTr("auffällig schwach")
        if (b === "strong" || b === "high" || b === "stark" || b === "1")
            return qsTr("auffällig stark")
        return qsTr("unauffällig")
    }

    // A skill entry may be a bare key, or a map. Read it without insisting on
    // a shape TeacherEngine has not promised.
    function keyOf(entry) {
        if (entry === undefined || entry === null)
            return ""
        if (typeof entry === "object")
            return "" + (entry.key !== undefined ? entry.key
                        : (entry.dimension !== undefined ? entry.dimension : ""))
        return "" + entry
    }

    function labelOf(entry) {
        if (entry !== null && typeof entry === "object" && entry.name !== undefined)
            return "" + entry.name
        return nameFor(keyOf(entry))
    }

    function bandOf(entry) {
        if (entry === null || typeof entry !== "object")
            return ""
        if (entry.band !== undefined) return entry.band
        if (entry.level !== undefined) return entry.level
        return ""
    }

    // 0..1 if there is anything to draw a bar from, otherwise -1.
    function shareOf(entry) {
        if (entry === null || typeof entry !== "object")
            return -1
        var v = entry.share !== undefined ? entry.share
              : (entry.value !== undefined ? entry.value : undefined)
        if (v === undefined || isNaN(v))
            return -1
        v = Number(v)
        if (v > 1)                       // a percentage, or an Elo-like number
            v = v > 100 ? -1 : v / 100
        return (v < 0 || v > 1) ? -1 : v
    }
}
