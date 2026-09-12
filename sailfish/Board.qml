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
import Sailfish.Silica 1.0
import "."

// The board of docs/design.md §7: a GridView of 64 cells over a ListModel,
// pieces as SVG Images, turned with layoutDirection the way shakkikello shows
// it can be done (rebuilt, not copied). Move entry is tap-the-piece then
// tap-the-target; nothing is dragged, because a dragged piece hides the square
// it is going to under the finger.
//
// Square numbering. `play(from, to)`, `legalTargets` and `selectedSquare` speak
// in square numbers, and `squares` is 64 entries long. This file assumes the
// numbering of Disservin/chess-library, which docs/design.md §2 vendors:
//
//     0 = a1, 1 = b1 … 7 = h1, 8 = a2 … 63 = h8
//
// The assumption lives in squareOfCell() alone. If TeacherEngine counts from
// a8 instead, that one function changes and nothing else does.
Item {
    id: board

    // The side of the board. Kept square, and never below minimumSide.
    property real side: Math.max(minimumSide, Math.min(width, height))
    readonly property real squareSide: side / 8

    // A square is never narrower than 11 % of the screen width, so a piece is
    // never smaller than a finger (Style.minimumSquareFraction).
    readonly property real minimumSquare: Screen.width * Style.minimumSquareFraction
    readonly property real minimumSide: minimumSquare * 8

    property bool interactive: true
    property bool showCoordinates: Prefs.showCoordinates
    property bool showLegalTargets: Prefs.showLegalTargets
    property bool showLastMove: Prefs.markLastMove

    // Read once per position change instead of 64 times per delegate.
    readonly property var squares: teacher.squares
    readonly property var targets: teacher.legalTargets
    readonly property int selected: teacher.selectedSquare
    readonly property bool flipped: teacher.flipped

    signal movePlayed(int from, int to, string promotion)
    signal moveRejected(int from, int to)

    implicitWidth: minimumSide
    implicitHeight: minimumSide

    // ---- Square arithmetic ------------------------------------------------

    // Cell 0 of the grid is the top left one; with White at the bottom that is
    // a8. layoutDirection and verticalLayoutDirection turn the whole grid when
    // the board is flipped, so this mapping never changes.
    function squareOfCell(cell) {
        var row = Math.floor(cell / 8)          // 0 at the top
        var col = cell % 8
        return (7 - row) * 8 + col
    }

    function fileOf(sq) { return sq % 8 }
    function rankOf(sq) { return Math.floor(sq / 8) }
    function squareName(sq) {
        if (sq < 0 || sq > 63)
            return ""
        return "abcdefgh".charAt(fileOf(sq)) + (rankOf(sq) + 1)
    }
    function squareByName(name) {
        if (!name || name.length < 2)
            return -1
        var f = "abcdefgh".indexOf(name.charAt(0))
        var r = parseInt(name.charAt(1), 10) - 1
        if (f < 0 || isNaN(r) || r < 0 || r > 7)
            return -1
        return r * 8 + f
    }

    function pieceAt(sq) {
        var all = board.squares
        if (!all || all.length !== 64 || sq < 0 || sq > 63)
            return ""
        return PieceCode.normalise(all[sq])
    }

    // legalTargets may be a list of numbers or a list of maps; take both.
    function isTarget(sq) {
        var list = board.targets
        if (!list)
            return false
        for (var i = 0; i < list.length; ++i) {
            var t = list[i]
            if (typeof t === "object" && t !== null)
                t = (t.to !== undefined) ? t.to : t.square
            if (t === sq)
                return true
        }
        return false
    }

    // `lastMove` is one string. If it is in the long algebraic form the engine
    // speaks (e2e4, e7e8q) both squares are marked; if it is SAN, nothing is,
    // and that is better than marking the wrong square.
    readonly property int lastFrom: squareByName(("" + teacher.lastMove).substring(0, 2))
    readonly property int lastTo: squareByName(("" + teacher.lastMove).substring(2, 4))

    // ---- Move entry -------------------------------------------------------

    function needsPromotion(from, to) {
        if (PieceCode.normalise(pieceAt(from)).charAt(1) !== "p")
            return false
        var r = rankOf(to)
        return r === 0 || r === 7
    }

    function tap(sq) {
        if (!board.interactive)
            return
        var from = board.selected

        if (from === sq) {                       // tapping it again lets go
            teacher.selectedSquare = -1
            return
        }

        if (from >= 0 && isTarget(sq)) {
            if (needsPromotion(from, sq)) {
                promotion.ask(from, sq)
                return
            }
            play(from, sq, "")
            return
        }

        // Not a target: treat it as picking a new piece up, or as putting the
        // current one down when the square is empty.
        teacher.selectedSquare = pieceAt(sq) === "" ? -1 : sq
    }

    function play(from, to, promo) {
        if (teacher.play(from, to, promo))
            board.movePlayed(from, to, promo)
        else
            board.moveRejected(from, to)
    }

    // ---- The grid ---------------------------------------------------------

    ListModel {
        id: cells
        Component.onCompleted: {
            for (var i = 0; i < 64; ++i)
                cells.append({ "cell": i })
        }
    }

    Rectangle {
        id: frame
        width: board.side
        height: board.side
        anchors.centerIn: parent
        color: Style.boardEdge
        border.color: Style.boardEdge
        border.width: Math.max(1, Math.round(board.squareSide * 0.02))

        GridView {
            id: grid
            anchors.fill: parent
            anchors.margins: frame.border.width
            cellWidth: (width) / 8
            cellHeight: cellWidth
            interactive: false
            clip: true
            model: cells

            // The whole turn of the board: both axes reversed, which is a
            // 180-degree rotation of the layout without rotating the pieces.
            layoutDirection: board.flipped ? Qt.RightToLeft : Qt.LeftToRight
            verticalLayoutDirection: board.flipped ? GridView.BottomToTop
                                                   : GridView.TopToBottom

            delegate: Item {
                id: cellItem
                width: grid.cellWidth
                height: grid.cellHeight

                readonly property int sq: board.squareOfCell(model.cell)
                readonly property bool darkSquare: (board.fileOf(sq) + board.rankOf(sq)) % 2 === 0
                readonly property string code: board.pieceAt(sq)
                readonly property bool isSelected: board.selected === sq
                readonly property bool isMoveTarget: board.showLegalTargets
                                                     && board.selected >= 0
                                                     && board.isTarget(sq)
                readonly property bool isLastMove: board.showLastMove
                                                   && (sq === board.lastFrom || sq === board.lastTo)

                Rectangle {
                    anchors.fill: parent
                    color: cellItem.darkSquare ? Style.darkSquare : Style.lightSquare
                }

                Rectangle {
                    anchors.fill: parent
                    visible: cellItem.isLastMove && !cellItem.isSelected
                    color: Style.lastMoveColor
                    opacity: 0.32
                }

                Rectangle {
                    anchors.fill: parent
                    visible: cellItem.isSelected
                    color: "transparent"
                    border.color: Style.selectionColor
                    border.width: Math.max(2, Math.round(cellItem.width * 0.07))
                }

                // Where the selected piece may go: a dot on an empty square, a
                // ring around a piece that can be taken.
                Rectangle {
                    anchors.centerIn: parent
                    visible: cellItem.isMoveTarget && cellItem.code === ""
                    width: cellItem.width * 0.30
                    height: width
                    radius: width / 2
                    color: Style.targetColor
                    opacity: 0.7
                }
                Rectangle {
                    anchors.centerIn: parent
                    visible: cellItem.isMoveTarget && cellItem.code !== ""
                    width: cellItem.width * 0.92
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.color: Style.targetColor
                    border.width: Math.max(2, Math.round(cellItem.width * 0.07))
                    opacity: 0.85
                }

                Piece {
                    anchors.centerIn: parent
                    width: cellItem.width * 0.94
                    height: width
                    piece: cellItem.code
                }

                // Coordinates on the rank the board starts at and on the file
                // beside it. Which edge that is follows the turn of the board.
                Text {
                    visible: board.showCoordinates
                             && board.fileOf(cellItem.sq) === (board.flipped ? 7 : 0)
                    x: cellItem.width * 0.06
                    y: cellItem.height * 0.04
                    text: board.rankOf(cellItem.sq) + 1
                    color: cellItem.darkSquare ? Style.lightSquare : Style.darkSquare
                    font.pixelSize: Math.max(8, cellItem.height * 0.22)
                }
                Text {
                    visible: board.showCoordinates
                             && board.rankOf(cellItem.sq) === (board.flipped ? 7 : 0)
                    x: cellItem.width * 0.72
                    y: cellItem.height * 0.72
                    text: "abcdefgh".charAt(board.fileOf(cellItem.sq))
                    color: cellItem.darkSquare ? Style.lightSquare : Style.darkSquare
                    font.pixelSize: Math.max(8, cellItem.height * 0.22)
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: board.interactive
                    onClicked: board.tap(cellItem.sq)
                }
            }
        }

        // ---- Promotion ----------------------------------------------------
        // Four pieces across the board instead of a dialog: the choice belongs
        // to the move, and the move is happening here.
        Rectangle {
            id: promotion
            anchors.centerIn: parent
            width: frame.width * 0.92
            height: board.squareSide * 1.3
            radius: Style.paddingMedium
            color: Qt.rgba(0, 0, 0, 0.86)
            visible: false
            z: 10

            property int from: -1
            property int to: -1

            function ask(fromSquare, toSquare) {
                promotion.from = fromSquare
                promotion.to = toSquare
                promotion.visible = true
            }
            function choose(type) {
                var f = promotion.from
                var t = promotion.to
                promotion.visible = false
                promotion.from = -1
                promotion.to = -1
                board.play(f, t, type)
            }

            MouseArea { anchors.fill: parent }   // swallow taps on the backdrop

            Row {
                anchors.centerIn: parent
                spacing: Style.paddingMedium

                Repeater {
                    model: ["q", "r", "b", "n"]

                    Item {
                        width: board.squareSide
                        height: board.squareSide

                        Piece {
                            anchors.fill: parent
                            piece: (teacher.whiteToMove ? "w" : "b") + modelData
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: promotion.choose(modelData)
                        }
                    }
                }
            }
        }
    }

    // Letting go of the selection when the position changes under it, e.g.
    // after the engine has answered.
    Connections {
        target: teacher
        onPositionChanged: {
            if (promotion.visible) {
                promotion.visible = false
                promotion.from = -1
                promotion.to = -1
            }
        }
    }
}
