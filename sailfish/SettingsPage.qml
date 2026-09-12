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

// Settings (docs/design.md §7). The split follows who owns the decision:
// the board orientation is a property of TeacherEngine because the engine has
// to know which way round the learner sees the position; everything else here
// only changes what the screen does with an answer and lives in Prefs.
//
// What is deliberately NOT here: difficulty, session length, the number of new
// patterns a day. Those are the method's to decide (teacher.md §5, §7), and a
// slider that lets the learner set them is a slider that lets the learner opt
// out of the part that works.
Page {
    id: page
    objectName: "settingsPage"
    allowedOrientations: Orientation.All

    Component.onCompleted: Prefs.adopt(teacher)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: qsTr("Einstellungen")
                description: qsTr("Brett und Darstellung")
            }

            SectionHeader { text: qsTr("Brett") }

            TextSwitch {
                width: parent.width
                text: qsTr("Von Schwarz aus")
                description: qsTr("Dreht das Brett um. Im Sparring dreht es sich ohnehin auf deine Farbe.")
                checked: teacher.flipped
                onCheckedChanged: {
                    if (teacher.flipped !== checked)
                        teacher.flipped = checked
                }
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Koordinaten anzeigen")
                description: qsTr("Linien- und Reihenbezeichnung am Rand des Bretts")
                checked: Prefs.showCoordinates
                onCheckedChanged: Prefs.showCoordinates = checked
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Mögliche Zielfelder markieren")
                description: qsTr("Punkte auf den Feldern, die die angetippte Figur erreichen kann. Im Einstufungstest bleiben sie aus.")
                checked: Prefs.showLegalTargets
                onCheckedChanged: Prefs.showLegalTargets = checked
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Letzten Zug markieren")
                description: qsTr("Hebt Ausgangs- und Zielfeld des zuletzt gespielten Zuges hervor")
                checked: Prefs.markLastMove
                onCheckedChanged: Prefs.markLastMove = checked
            }

            SectionHeader { text: qsTr("Bewertung") }

            TextSwitch {
                width: parent.width
                text: qsTr("Zahlen zusätzlich anzeigen")
                description: qsTr("Die Erklärung im Klartext steht immer da. Zusätzlich die Bewertung in Bauerneinheiten und die Gewinnaussicht in Prozent — hilfreich, sobald du weißt, was diese Zahlen nicht bedeuten.")
                checked: Prefs.showNumbers
                onCheckedChanged: Prefs.showNumbers = checked
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Eine Bewertungszahl ohne Satz gibt es in dieser App nicht. „+1,4“ sagt nicht, was zu tun ist; „Dein Läufer steht ungedeckt und der Springer greift ihn an“ sagt es.")
            }

            SectionHeader { text: qsTr("Engine") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: teacher.engineReady ? Style.secondaryColor : Style.errorColor
                text: teacher.engineReady
                      ? qsTr("Die Engine läuft. Sparring, Einstufung und Auswertung stehen zur Verfügung.")
                      : qsTr("Die Engine läuft nicht. Brett, Karten und Regeln funktionieren trotzdem.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Stockfish läuft als eigenes Programm neben der App und wird über UCI angesprochen. Die Endspieldatenbanken für drei und vier Steine sind im Paket enthalten; es wird nichts nachgeladen.")
            }

            SectionHeader { text: qsTr("Zurücksetzen") }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Darstellung zurücksetzen")
                onClicked: {
                    Prefs.showNumbers = false
                    Prefs.showCoordinates = true
                    Prefs.showLegalTargets = true
                    Prefs.markLastMove = true
                    teacher.flipped = false
                }
            }
        }
    }
}
