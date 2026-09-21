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

// Stepping through the solution on the board (chess-spec/teacher.md §6.5: the
// app plays the sequence back; §6.6 hint level 4: the full solution with a
// sentence).
//
// Forward one half move at a time and not as an animation, because a line is
// read, not watched: the learner decides when the opponent's answer appears,
// which is the same demand the task itself makes of them.
Column {
    id: panel

    property var view: teacher.solutionView
    property bool active: view && view.active === true

    visible: active
    height: visible ? implicitHeight : 0
    spacing: Style.paddingSmall

    // The whole line, in German notation, worked out on the position it was
    // asked in. Naming it here is the coding step of §6.2 — after solving,
    // never before. The moves already on the board are lit, the rest are not,
    // so the strip shows where the board stands without a second counter.
    Flow {
        x: Style.horizontalPageMargin
        width: panel.width - 2 * Style.horizontalPageMargin
        spacing: Style.paddingMedium

        Repeater {
            model: panel.view && panel.view.sanMoves ? panel.view.sanMoves : 0
            Label {
                text: modelData
                font.pixelSize: Style.fontSizeSmall
                color: index < (panel.view ? panel.view.step : 0)
                       ? Style.highlightColor : Style.secondaryColor
            }
        }
    }

    Label {
        x: Style.horizontalPageMargin
        width: panel.width - 2 * Style.horizontalPageMargin
        visible: text !== ""
        wrapMode: Text.WordWrap
        color: Style.secondaryColor
        font.pixelSize: Style.fontSizeExtraSmall
        // Why the move is right, not what it wins (§6.6). The sentence that
        // says what the learner actually played is the feedback strip's job
        // and is not repeated here.
        text: panel.view && panel.view.explanation ? panel.view.explanation : ""
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Style.paddingMedium

        IconButton {
            icon.source: "image://theme/icon-m-back"
            enabled: panel.view && panel.view.atStart !== true
            onClicked: teacher.solutionBack()
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            color: Style.secondaryColor
            font.pixelSize: Style.fontSizeExtraSmall
            // A count of half moves, and the one that comes next. Not "Matt in
            // drei" — that would name the sort of task (§6.2), and the learner
            // may still be stepping through it.
            text: panel.view
                  ? (panel.view.atEnd
                     ? qsTr("Halbzug %1 von %2").arg(panel.view.total).arg(panel.view.total)
                     : qsTr("Halbzug %1 von %2 · weiter mit %3")
                       .arg(panel.view.step).arg(panel.view.total)
                       .arg(panel.view.nextSan ? panel.view.nextSan : "?"))
                  : ""
        }

        IconButton {
            icon.source: "image://theme/icon-m-forward"
            enabled: panel.view && panel.view.atEnd !== true
            onClicked: teacher.solutionForward()
        }
    }

    Button {
        anchors.horizontalCenter: parent.horizontalCenter
        text: qsTr("Weiter üben")
        onClicked: teacher.hideSolution()
    }
}
