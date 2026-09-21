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

// "Die Engine überlegt" as a sentence plus a spinner, never as a blocked UI:
// requests are serial and time-boxed (docs/design.md §5), so this is short.
Row {
    id: indicator

    property string label: qsTr("Die Engine ueberlegt ...")

    spacing: Style.paddingMedium
    visible: teacher.thinking
    height: visible ? Math.max(spinner.height, text.height) : 0

    BusyIndicator {
        id: spinner
        width: AppTheme.itemSizeSmall
        height: AppTheme.itemSizeSmall
        running: indicator.visible
        anchors.verticalCenter: parent.verticalCenter
    }

    Label {
        id: text
        anchors.verticalCenter: parent.verticalCenter
        text: indicator.label
        color: Style.secondaryColor
        font.pixelSize: Style.fontSizeExtraSmall
    }
}
