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

// The cover says what is due today and nothing else; a board at cover size is
// unreadable and a number without a sentence is against the rule anyway.
CoverBackground {
    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Style.paddingMedium
        spacing: Style.paddingMedium

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Schachlehrer")
            font.pixelSize: Style.fontSizeMedium
            color: Style.highlightColor
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeExtraSmall
            color: Style.secondaryColor
            text: teacher.dueCards > 0
                  ? qsTr("%n Karten fällig", "", teacher.dueCards)
                  : qsTr("Nichts fällig")
        }
    }

    CoverActionList {
        id: coverAction
        CoverAction {
            iconSource: "image://theme/icon-cover-play"
            onTriggered: {
                teacher.startSession()
                app.activate()
            }
        }
    }
}
