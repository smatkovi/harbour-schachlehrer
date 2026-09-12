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

// „Was frage ich mich?" — the thinking routine (teacher.md §6.6, §7.5).
//
// The list is the learner's own: TeacherEngine.routine is ordered by his error
// record and hides what he has not needed for a long time, so this component
// only draws what it is given and never sorts or filters by itself.
//
// Collapsed by default. The routine is meant to be run in the head; a panel
// that is open all the time is the poster this was supposed to replace.
Item {
    id: panel

    property var questions: teacher.routine
    property bool expanded: false

    // Only the ones that have not retired (§7.5: the scaffolding disappears as
    // it is internalised). `visible` comes from the engine, not from here.
    readonly property var shown: {
        var out = []
        var list = panel.questions
        if (!list)
            return out
        for (var i = 0; i < list.length; ++i) {
            if (list[i].visible)
                out.push(list[i])
        }
        return out
    }
    readonly property int count: shown.length

    width: parent ? parent.width : 0
    height: count > 0 ? column.height : 0
    visible: count > 0

    Column {
        id: column
        width: parent.width

        BackgroundItem {
            id: header
            width: parent.width
            height: Style.itemSizeSmall
            onClicked: panel.expanded = !panel.expanded

            Label {
                x: Style.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * Style.horizontalPageMargin - chevron.width
                       - Style.paddingMedium
                text: qsTr("Was frage ich mich?")
                color: header.highlighted ? Style.highlightColor : Style.primaryColor
                font.pixelSize: Style.fontSizeSmall
                truncationMode: TruncationMode.Fade
            }

            Image {
                id: chevron
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: Style.horizontalPageMargin
                source: panel.expanded ? "image://theme/icon-m-up" : "image://theme/icon-m-down"
            }
        }

        Column {
            width: parent.width
            visible: panel.expanded
            spacing: Style.paddingMedium

            Repeater {
                model: panel.shown

                Item {
                    width: panel.width
                    height: row.height

                    Column {
                        id: row
                        x: Style.horizontalPageMargin
                        width: panel.width - 2 * Style.horizontalPageMargin
                        spacing: Style.paddingSmall / 2

                        Label {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            font.pixelSize: Style.fontSizeSmall
                            // The number is the position in his own list, which
                            // is what the feedback quotes ("Frage 2 hätte das
                            // gefunden"). It is a label, not a score (§6.6).
                            text: modelData.rank + ". " + modelData.text
                            color: modelData.caught ? Style.highlightColor
                                   : modelData.standing === "verblassend" ? Style.secondaryColor
                                                                          : Style.primaryColor
                        }

                        Label {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            font.pixelSize: Style.fontSizeTiny
                            color: Style.secondaryColor
                            text: modelData.caught
                                  ? qsTr("%1 · die hat zuletzt etwas gefunden").arg(modelData.when)
                                  : modelData.when
                        }
                    }
                }
            }

            Label {
                x: Style.horizontalPageMargin
                width: panel.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeTiny
                color: Style.secondaryColor
                // QML is JavaScript: adjacent string literals do not
                // concatenate, they are a syntax error. Hence the plus.
                text: qsTr("Die Reihenfolge kommt aus deinen eigenen Partien. Was du lange "
                           + "nicht mehr falsch gemacht hast, verschwindet aus der Liste.")
            }

            // Qt 5.6 positioners have padding, but a spacer is one thing less
            // that can differ between the target and the build host.
            Item {
                width: 1
                height: Style.paddingSmall
            }
        }
    }
}
