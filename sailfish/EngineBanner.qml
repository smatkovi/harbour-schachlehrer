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

// A missing engine is a normal state, not a crash (docs/design.md §5). The app
// says so in plain German and stays usable: board, cards and rules work
// without it, sparring and analysis do not.
Item {
    id: banner

    readonly property bool missing: !teacher.engineReady

    width: parent ? parent.width : 0
    height: missing ? row.height + 2 * Style.paddingMedium : 0
    visible: missing

    Rectangle {
        anchors.fill: parent
        color: Style.errorColor
        opacity: 0.16
        radius: Style.paddingSmall
    }

    Column {
        id: row
        x: Style.horizontalPageMargin
        y: Style.paddingMedium
        width: parent.width - 2 * Style.horizontalPageMargin
        spacing: Style.paddingSmall

        Label {
            width: parent.width
            text: qsTr("Die Schach-Engine läuft nicht.")
            color: Style.primaryColor
            font.pixelSize: Style.fontSizeSmall
            wrapMode: Text.WordWrap
        }
        Label {
            width: parent.width
            text: qsTr("Brett, Karten und Regeln kannst du weiter benutzen. Sparring, Einstufung und die Analyse einer Partie brauchen die Engine und bleiben so lange aus.")
            color: Style.secondaryColor
            font.pixelSize: Style.fontSizeExtraSmall
            wrapMode: Text.WordWrap
        }
    }
}
