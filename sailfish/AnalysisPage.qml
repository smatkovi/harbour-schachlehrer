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

// The diagnosis step of the loop (docs/design.md §7): the move list of the
// finished game with what the analysis found in it. Every finding is a
// sentence about one move; the class and the dimension are there so the
// learner can see the pattern across several games, and a finding that makes a
// card says so, because that is what happens next.
Page {
    id: page
    objectName: "analysisPage"
    allowedOrientations: Orientation.All

    property var findings: []

    function reload() {
        page.findings = teacher.lastFindings()
    }

    Component.onCompleted: page.reload()

    Connections {
        target: teacher
        onProgressChanged: page.reload()
        onPositionChanged: page.reload()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        PullDownMenu {
            MenuItem {
                text: qsTr("Noch einmal auswerten")
                enabled: teacher.engineReady && !teacher.thinking
                onClicked: teacher.analyseCurrentGame()
            }
        }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: qsTr("Auswertung")
                description: teacher.gameResult !== "" ? teacher.gameResult : qsTr("Die letzte Partie")
            }

            EngineBanner { }

            ThinkingIndicator {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                label: qsTr("Die Partie wird Zug für Zug durchgerechnet …")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: (!teacher.moveList || teacher.moveList.length === 0) && !teacher.thinking
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                text: qsTr("Noch keine Partie ausgewertet. Spiele eine Partie zu Ende, dann steht sie hier Zug für Zug.")
            }

            // ---- Was gefunden wurde --------------------------------------
            SectionHeader {
                text: qsTr("Gefunden")
                visible: page.findings && page.findings.length > 0
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.findings && page.findings.length > 0
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("%n Stellen, an denen etwas schiefging. Aus den meisten wird eine Übungskarte.", "", page.findings.length)
            }

            Repeater {
                model: page.findings

                ListItem {
                    width: page.width
                    contentHeight: finding.height + 2 * Style.paddingMedium

                    Column {
                        id: finding
                        x: Style.horizontalPageMargin
                        width: parent.width - 2 * Style.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Style.paddingSmall / 2

                        // The sentence first. The class key is a label for it,
                        // not a replacement.
                        Label {
                            width: parent.width
                            text: {
                                if (modelData && modelData.text !== undefined)
                                    return "" + modelData.text
                                if (modelData && modelData.playedMove !== undefined)
                                    return qsTr("%1 war nicht der beste Zug.").arg("" + modelData.playedMove)
                                return qsTr("Hier ging etwas schief.")
                            }
                            color: Style.primaryColor
                            wrapMode: Text.WordWrap
                            font.pixelSize: Style.fontSizeSmall
                        }

                        Label {
                            width: parent.width
                            visible: text !== ""
                            color: Style.secondaryColor
                            font.pixelSize: Style.fontSizeExtraSmall
                            wrapMode: Text.WordWrap
                            text: {
                                if (!modelData)
                                    return ""
                                var parts = []
                                if (modelData.playedMove !== undefined && modelData.bestMove !== undefined)
                                    parts.push(qsTr("gespielt %1, besser %2")
                                               .arg("" + modelData.playedMove)
                                               .arg("" + modelData.bestMove))
                                if (modelData.dimension !== undefined)
                                    parts.push(Dimensions.nameFor(modelData.dimension))
                                if (modelData.cls !== undefined)
                                    parts.push("" + modelData.cls)
                                return parts.join("  ·  ")
                            }
                        }

                        // teacher.md §6.6, the link back: which question of
                        // his routine would have caught this one. It stands
                        // after the sentence, never instead of it.
                        Label {
                            width: parent.width
                            visible: text !== ""
                            text: (modelData && modelData.questionSentence !== undefined)
                                  ? ("" + modelData.questionSentence) : ""
                            color: Style.highlightColor
                            wrapMode: Text.WordWrap
                            font.pixelSize: Style.fontSizeExtraSmall
                        }

                        Label {
                            width: parent.width
                            visible: modelData && modelData.makesCard === true
                            text: qsTr("Daraus wird eine Übungskarte.")
                            color: Style.highlightColor
                            font.pixelSize: Style.fontSizeTiny
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // ---- Die Zugliste ---------------------------------------------
            SectionHeader {
                text: qsTr("Zugliste")
                visible: teacher.moveList && teacher.moveList.length > 0
            }

            Repeater {
                model: teacher.moveList

                Item {
                    width: page.width
                    height: moveRow.height + Style.paddingSmall

                    Row {
                        id: moveRow
                        x: Style.horizontalPageMargin
                        width: parent.width - 2 * Style.horizontalPageMargin
                        spacing: Style.paddingMedium

                        Label {
                            width: Style.itemSizeExtraSmall
                            horizontalAlignment: Text.AlignRight
                            text: index % 2 === 0 ? (Math.floor(index / 2) + 1) + "." : ""
                            color: Style.secondaryColor
                            font.pixelSize: Style.fontSizeExtraSmall
                        }

                        Column {
                            width: parent.width - Style.itemSizeExtraSmall - Style.paddingMedium

                            Label {
                                width: parent.width
                                text: MoveList.san(modelData)
                                color: Style.primaryColor
                                font.pixelSize: Style.fontSizeSmall
                            }
                            Label {
                                width: parent.width
                                visible: text !== ""
                                text: MoveList.comment(modelData)
                                color: Style.secondaryColor
                                font.pixelSize: Style.fontSizeExtraSmall
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }
}
