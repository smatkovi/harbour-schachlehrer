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

// One of the six dimensions on the start page: the name, the ordinal verdict
// in words, and a bar only where there is a number worth drawing. No Elo, no
// percentage as the headline — teacher.md §4.6 says 25 items do not carry one.
Item {
    id: row

    property variant entry
    property string dimensionKey: Dimensions.keyOf(entry)
    property real share: Dimensions.shareOf(entry)

    width: parent ? parent.width : 0
    height: column.height + Style.paddingSmall

    Column {
        id: column
        x: Style.horizontalPageMargin
        width: parent.width - 2 * Style.horizontalPageMargin
        spacing: Style.paddingSmall / 2

        Item {
            width: parent.width
            height: name.height

            Label {
                id: name
                text: Dimensions.labelOf(row.entry)
                color: Style.primaryColor
                font.pixelSize: Style.fontSizeSmall
            }
            Label {
                anchors.right: parent.right
                anchors.baseline: name.baseline
                text: Dimensions.bandName(Dimensions.bandOf(row.entry))
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
            }
        }

        Rectangle {
            visible: row.share >= 0
            width: parent.width
            height: Math.max(2, Style.paddingSmall / 2)
            radius: height / 2
            color: Style.secondaryColor
            opacity: 0.3

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, row.share))
                height: parent.height
                radius: parent.radius
                color: Style.highlightColor
                opacity: 1.0
            }
        }

        Label {
            width: parent.width
            text: Dimensions.questionFor(row.dimensionKey)
            color: Style.secondaryColor
            font.pixelSize: Style.fontSizeTiny
            wrapMode: Text.WordWrap
            visible: text !== ""
        }
    }
}
