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
import com.nokia.meego 1.0

// Silica metrics under neutral names, plus the handful of values that are
// specific to a chessboard. Same shape as harbour-tarock/sailfish/Style.qml:
// one place to change the look, and no page reaching into Theme for something
// the board defines.
QtObject {
    property real paddingSmall: AppTheme.paddingSmall
    property real paddingMedium: AppTheme.paddingMedium
    property real paddingLarge: AppTheme.paddingLarge
    property real horizontalPageMargin: AppTheme.horizontalPageMargin
    property real itemSizeExtraSmall: AppTheme.itemSizeExtraSmall
    property real itemSizeSmall: AppTheme.itemSizeSmall
    property real itemSizeMedium: AppTheme.itemSizeMedium
    property real itemSizeLarge: AppTheme.itemSizeLarge
    property real fontSizeTiny: AppTheme.fontSizeTiny
    property real fontSizeExtraSmall: AppTheme.fontSizeExtraSmall
    property real fontSizeSmall: AppTheme.fontSizeSmall
    property real fontSizeMedium: AppTheme.fontSizeMedium
    property real fontSizeLarge: AppTheme.fontSizeLarge
    property color primaryColor: AppTheme.primaryColor
    property color secondaryColor: AppTheme.secondaryColor
    property color highlightColor: AppTheme.highlightColor
    property color secondaryHighlightColor: AppTheme.secondaryHighlightColor
    property color errorColor: AppTheme.errorColor

    // ---- The board -------------------------------------------------------
    // Warm wood, not the Silica palette: the board has to read the same in the
    // light and in the dark ambience, and the cburnett pieces are drawn for a
    // light board.
    property color lightSquare: "#e8d2b2"
    property color darkSquare: "#6c4c30"
    property color boardEdge: "#3a281a"
    property color coordinateColor: "#8a7050"

    property color selectionColor: "#f2c14e"   // the square you tapped
    property color targetColor: "#4c9f70"      // where that piece may go
    property color lastMoveColor: "#c8a24a"    // where the last move came from and went
    property color checkColor: "#c0392b"

    // A square is never narrower than this. 11 % of the screen width is about
    // 7.9 mm on a 540 px phone, i.e. above the 7 mm a finger needs, and eight
    // of them still leave a margin on the narrowest supported display.
    property real minimumSquareFraction: 0.11

    // Where the piece graphics live, relative to /usr/share/harbour-schachlehrer/qml.
    property string pieceSet: "../assets/pieces/cburnett/"
}
