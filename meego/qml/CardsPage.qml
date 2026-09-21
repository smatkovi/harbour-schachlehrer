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

// The card overview (docs/design.md §7). A card is a pair of pattern and
// dimension (teacher.md §5), it comes out of a mistake the learner actually
// made, and FSRS decides when it comes back. This page is deliberately thin:
// the interesting question is not "how many cards do I have" but "what is due
// today", and the answer to that is one number and one button.
//
// The per-card list arrives with M4; TeacherEngine exposes `dueCards` and
// nothing else about the deck yet (§4), so nothing else is claimed here.
Page {
    id: page
    objectName: "cardsPage"
    orientationLock: PageOrientation.Automatic

    Flickable {
        anchors.fill: parent
        pressDelay: 150
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: qsTr("Karten")
                description: teacher.dueCards > 0
                             ? qsTr("%n faellig", "", teacher.dueCards)
                             : qsTr("nichts faellig")
            }

            Item {
                width: parent.width
                height: Style.itemSizeLarge

                Label {
                    anchors.centerIn: parent
                    text: "" + teacher.dueCards
                    color: teacher.dueCards > 0 ? Style.highlightColor : Style.secondaryColor
                    font.pixelSize: Style.fontSizeLarge * 2
                }
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                text: teacher.dueCards > 0
                      ? qsTr("So viele Karten sind heute dran. Sechs Minuten reichen dafuer.")
                      : qsTr("Heute ist keine Karte faellig. Neue entstehen von selbst, sobald du eine Partie spielst und auswerten laesst.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Wiederholen")
                enabled: teacher.dueCards > 0
                onClicked: {
                    teacher.startSession()
                    pageStack.push(Qt.resolvedUrl("BoardPage.qml"))
                }
            }

            SectionHeader { text: qsTr("Woher die Karten kommen") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Aus deinen eigenen Partien. Nach jeder Partie wird jeder Zug nachgerechnet; wo etwas schiefging, wird der Fehler einer von fuenfzig Klassen zugeordnet, und aus den meisten dieser Klassen entsteht eine Uebungskarte mit genau diesem Muster.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Eine Karte ist ein Muster, keine einzelne Stellung. Sie gilt erst als gekonnt, wenn du sie in einer Stellung wiedererkennst, die du vorher nicht gesehen hast.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Der Termin kommt von FSRS: je sicherer du eine Karte beantwortest, desto weiter rueckt sie weg. Falsch beantwortet rueckt sie nah heran, aber nicht auf null.")
            }
        }
    }
}
