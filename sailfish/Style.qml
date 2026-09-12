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
pragma Singleton
import QtQuick 2.6
import Sailfish.Silica 1.0

// Silica metrics under neutral names, plus the handful of values that are
// specific to a chessboard. Same shape as harbour-tarock/sailfish/Style.qml:
// one place to change the look, and no page reaching into Theme for something
// the board defines.
QtObject {
    readonly property real paddingSmall: Theme.paddingSmall
    readonly property real paddingMedium: Theme.paddingMedium
    readonly property real paddingLarge: Theme.paddingLarge
    readonly property real horizontalPageMargin: Theme.horizontalPageMargin
    readonly property real itemSizeExtraSmall: Theme.itemSizeExtraSmall
    readonly property real itemSizeSmall: Theme.itemSizeSmall
    readonly property real itemSizeMedium: Theme.itemSizeMedium
    readonly property real itemSizeLarge: Theme.itemSizeLarge
    readonly property real fontSizeTiny: Theme.fontSizeTiny
    readonly property real fontSizeExtraSmall: Theme.fontSizeExtraSmall
    readonly property real fontSizeSmall: Theme.fontSizeSmall
    readonly property real fontSizeMedium: Theme.fontSizeMedium
    readonly property real fontSizeLarge: Theme.fontSizeLarge
    readonly property color primaryColor: Theme.primaryColor
    readonly property color secondaryColor: Theme.secondaryColor
    readonly property color highlightColor: Theme.highlightColor
    readonly property color secondaryHighlightColor: Theme.secondaryHighlightColor
    readonly property color errorColor: Theme.errorColor

    // ---- The board -------------------------------------------------------
    // Warm wood, not the Silica palette: the board has to read the same in the
    // light and in the dark ambience, and the cburnett pieces are drawn for a
    // light board.
    readonly property color lightSquare: "#e8d2b2"
    readonly property color darkSquare: "#6c4c30"
    readonly property color boardEdge: "#3a281a"
    readonly property color coordinateColor: "#8a7050"

    readonly property color selectionColor: "#f2c14e"   // the square you tapped
    readonly property color targetColor: "#4c9f70"      // where that piece may go
    readonly property color lastMoveColor: "#c8a24a"    // where the last move came from and went
    readonly property color checkColor: "#c0392b"

    // A square is never narrower than this. 11 % of the screen width is about
    // 7.9 mm on a 540 px phone, i.e. above the 7 mm a finger needs, and eight
    // of them still leave a margin on the narrowest supported display.
    readonly property real minimumSquareFraction: 0.11

    // Where the piece graphics live, relative to /usr/share/harbour-schachlehrer/qml.
    readonly property string pieceSet: "../assets/pieces/cburnett/"
}
