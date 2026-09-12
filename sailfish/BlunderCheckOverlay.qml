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

// The blunder-check drill (teacher.md §7.5): before the move is released, the
// opponent's checks and captures, and the learner marks the ones that cost him
// something. Only then does his own move go through.
//
// Compact on purpose — it sits over the board, not instead of it, so that the
// learner answers by looking at the position and not at a wall of text. The
// moves are shown in SAN and nothing else: the SEE is the answer, and a number
// has no business here anyway (§6.6).
Item {
    id: overlay

    property var check: teacher.blunderCheck
    readonly property bool active: check !== undefined && check !== null
                                   && check.active === true
    readonly property var moves: (check && check.moves) ? check.moves : []

    // The indices the learner has marked as dangerous.
    property var marked: []

    function toggle(index) {
        var out = []
        var found = false
        for (var i = 0; i < marked.length; ++i) {
            if (marked[i] === index)
                found = true
            else
                out.push(marked[i])
        }
        if (!found)
            out.push(index)
        marked = out
    }

    function isMarked(index) {
        for (var i = 0; i < marked.length; ++i) {
            if (marked[i] === index)
                return true
        }
        return false
    }

    onActiveChanged: marked = []

    width: parent ? parent.width : 0
    height: active ? frame.height : 0
    visible: active

    Rectangle {
        id: frame
        width: parent.width
        height: content.height + 2 * Style.paddingMedium
        color: Style.secondaryHighlightColor
        opacity: 0.18
    }

    Column {
        id: content
        y: Style.paddingMedium
        width: parent.width
        spacing: Style.paddingSmall

        Label {
            x: Style.horizontalPageMargin
            width: parent.width - 2 * Style.horizontalPageMargin
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeSmall
            color: Style.highlightColor
            text: (overlay.check && overlay.check.prompt) ? overlay.check.prompt : ""
        }

        // The question this drill is: the same sentence as in the routine
        // panel, so the two are visibly one thing and not two features.
        Label {
            x: Style.horizontalPageMargin
            width: parent.width - 2 * Style.horizontalPageMargin
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeTiny
            color: Style.secondaryColor
            visible: text !== ""
            text: (overlay.check && overlay.check.question) ? overlay.check.question : ""
        }

        Flow {
            x: Style.horizontalPageMargin
            width: parent.width - 2 * Style.horizontalPageMargin
            spacing: Style.paddingSmall

            Repeater {
                model: overlay.moves

                BackgroundItem {
                    id: chip
                    width: label.width + 2 * Style.paddingMedium
                    height: Style.itemSizeExtraSmall
                    onClicked: overlay.toggle(modelData.index)

                    Rectangle {
                        anchors.fill: parent
                        radius: Style.paddingSmall
                        color: overlay.isMarked(modelData.index) ? Style.highlightColor
                                                                 : "transparent"
                        opacity: 0.35
                        border.width: 1
                        border.color: Style.secondaryColor
                    }

                    Label {
                        id: label
                        anchors.centerIn: parent
                        text: modelData.san !== "" ? modelData.san : modelData.uci
                        font.pixelSize: Style.fontSizeSmall
                        color: overlay.isMarked(modelData.index) ? Style.highlightColor
                                                                 : Style.primaryColor
                    }
                }
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Style.paddingMedium

            Button {
                text: qsTr("Nichts davon")
                visible: overlay.marked.length === 0
                onClicked: teacher.answerBlunderCheck([])
            }

            Button {
                text: qsTr("Zug freigeben")
                visible: overlay.marked.length > 0
                onClicked: teacher.answerBlunderCheck(overlay.marked)
            }

            Button {
                text: qsTr("Überspringen")
                onClicked: teacher.skipBlunderCheck()
            }
        }
    }
}
