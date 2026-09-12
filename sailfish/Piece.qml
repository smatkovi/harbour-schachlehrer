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
import QtQuick 2.6
import "."

// One chess piece as a cburnett SVG (BSD-3-Clause, CREDITS/ASSETS.md).
//
// `piece` is what TeacherEngine put into `squares`. Which spelling that is has
// not been nailed down, so both of the plausible ones are accepted:
//
//   FEN letter      "K" white king, "k" black king, "" empty
//   colour + type   "wK", "bq", "wn" …
//
// Anything else renders as an empty square rather than as a broken image.
Item {
    id: root

    property string piece: ""

    // "wq", "bk" … or "" — the normalised form, useful to callers as well.
    readonly property string code: PieceCode.normalise(piece)
    readonly property string colour: code === "" ? "" : code.charAt(0)
    readonly property string type: code === "" ? "" : code.charAt(1)

    // Chess_<type><l|d>t45.svg, the Wikimedia Commons file naming.
    readonly property url source: code === ""
        ? ""
        : Qt.resolvedUrl(Style.pieceSet + "Chess_" + type + (colour === "w" ? "l" : "d") + "t45.svg")

    implicitWidth: Style.itemSizeSmall
    implicitHeight: implicitWidth

    Image {
        id: image
        anchors.fill: parent
        source: root.source
        visible: root.code !== "" && status === Image.Ready
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: false
        // The originals are 45x45; render them at the size they are shown at,
        // not at 45 px and then scaled, or they turn to mush on a hi-dpi phone.
        sourceSize.width: Math.max(1, Math.round(root.width))
        sourceSize.height: Math.max(1, Math.round(root.height))
    }

    // Falls back to the letter if the SVG is missing, so a broken asset does
    // not silently produce an empty board.
    Text {
        anchors.centerIn: parent
        visible: root.code !== "" && image.status !== Image.Ready
        text: root.type.toUpperCase()
        color: root.colour === "w" ? "#fafafa" : "#141414"
        font.pixelSize: Math.max(8, root.height * 0.6)
        font.bold: true
    }
}
