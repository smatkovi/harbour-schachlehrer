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
import "."

// How much of the answer is still to come (chess-spec/teacher.md §6.5).
//
// A count of dots and nothing else. It must not say "Matt in drei" or "Gabel"
// or anything of the sort: naming the kind of task before it is solved is
// exactly what §6.2 forbids, and it is the one principle of the whole
// specification with two independent sources behind it (Stappenmethode,
// Chess Tempo). "Noch zwei Züge" says how long the answer is, not what it is.
//
// It shows itself only for tasks that are longer than one move — on a
// one-move task there is nothing to count and a lone dot would be noise.
Column {
    id: panel

    property variant task: teacher.task
    property int total: task && task.moves !== undefined ? Number(task.moves) : 1
    property int entered: task && task.entered !== undefined ? Number(task.entered) : 0
    property bool guided: task && task.guided === true
    property bool multiMove: task && task.multiMove === true

    visible: multiMove && !teacher.reviewing
    height: visible ? implicitHeight : 0
    spacing: Style.paddingSmall

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Style.paddingSmall

        Repeater {
            model: panel.total
            Rectangle {
                width: Style.paddingMedium
                height: width
                radius: width / 2
                // Filled for what is entered, hollow for what is still to come.
                color: index < panel.entered ? Style.highlightColor : "transparent"
                border.width: index < panel.entered ? 0 : 1
                border.color: Style.secondaryColor
                opacity: index < panel.entered ? 1.0 : 0.6
            }
        }
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        color: Style.secondaryColor
        font.pixelSize: Style.fontSizeExtraSmall
        horizontalAlignment: Text.AlignHCenter
        width: panel.width - 2 * Style.horizontalPageMargin
        wrapMode: Text.WordWrap
        // Two different situations, and the difference is worth a sentence:
        // normally the learner enters the opponent's replies too, which is the
        // whole point (§6.5); in the one guided instance of a new pattern the
        // app answers, and then it says so rather than letting the board move
        // on its own without explanation.
        text: panel.guided
              ? qsTr("Ich ziehe fuer deinen Gegner. Du bist wieder dran.")
              : qsTr("Gib die ganze Folge ein - auch die Zuege deines Gegners.")
    }

    Button {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: panel.entered > 0
        text: qsTr("Zug zurueck")
        // §7.6: taking back is free. Composing a line is not answering, and a
        // slip of the finger must not be counted as a wrong answer.
        onClicked: teacher.undoEntry()
    }
}
