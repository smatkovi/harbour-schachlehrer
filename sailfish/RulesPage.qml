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

// Regeln und Glossar (docs/design.md §7). Works without the engine — that is
// the point of having it: when the engine is missing, this and the cards are
// what is left, and they have to be worth opening.
//
// The searchable rule browser and the full glossary come with M7; what stands
// here is the part a learner needs on the first evening, written out rather
// than linked.
Page {
    id: page
    objectName: "rulesPage"
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: qsTr("Regeln und Glossar")
                description: qsTr("Nachschlagen, ohne die Engine")
            }

            Repeater {
                model: [
                    { head: qsTr("Wie die Figuren ziehen"),
                      body: qsTr("Der Turm gerade, der Läufer schräg, die Dame beides, der König ein Feld in jede Richtung. Der Springer springt im L und ist die einzige Figur, die über andere hinweggeht. Der Bauer zieht vorwärts, schlägt aber schräg — der häufigste Anfängerfehler steckt genau in diesem Unterschied.") },
                    { head: qsTr("Die drei Sonderzüge"),
                      body: qsTr("Rochade: König zwei Felder zum Turm, Turm daneben. Nur wenn beide noch nicht gezogen haben, die Felder frei sind und der König weder im Schach steht noch durch ein bedrohtes Feld geht. En passant: ein Bauer, der gerade zwei Felder vorgezogen ist, darf im unmittelbar folgenden Zug geschlagen werden, als wäre er nur eines gezogen. Umwandlung: ein Bauer auf der letzten Reihe wird zu Dame, Turm, Läufer oder Springer — nie zum König und nie zum Bauern.") },
                    { head: qsTr("Wann die Partie aus ist"),
                      body: qsTr("Matt: der König steht im Schach und kein Zug hebt das auf. Patt: kein legaler Zug, aber kein Schach — remis, und die häufigste vergebene Gewinnstellung im Endspiel. Remis außerdem bei dreifacher Stellungswiederholung, nach fünfzig Zügen ohne Schlag und ohne Bauernzug, und wenn keine Seite mehr mattsetzen kann.") },
                    { head: qsTr("Der Materialwert"),
                      body: qsTr("Bauer 1, Springer und Läufer je etwa 3, Turm 5, Dame 9. Der König hat keinen Wert, weil man ihn nicht tauschen kann. Die Zahlen sind eine Faustregel für den Tausch, kein Urteil über die Stellung: zwei Läufer sind mehr wert als ihre sechs Punkte, ein eingesperrter Turm weniger als seine fünf.") },
                    { head: qsTr("Hängt etwas?"),
                      body: qsTr("Eine Figur hängt, wenn sie geschlagen werden kann und nicht gedeckt ist — oder wenn der Schlagende weniger wert ist als sie. Vor jedem Zug drei Fragen: Steht etwas von mir im Schlag? Droht der Gegner etwas? Und was ändert mein Zug daran? Das ist die Dimension Sorgfalt, und sie kostet mehr Partien als jede Eröffnungslücke.") },
                    { head: qsTr("Die drei Grundmotive"),
                      body: qsTr("Gabel: eine Figur greift zwei an. Fesselung: eine Figur darf nicht wegziehen, weil dahinter etwas Wertvolleres steht. Spieß: dasselbe umgekehrt — vorne das Wertvolle, das wegziehen muss, und dahinter fällt die zweite Figur. Drei Viertel aller Widerlegungen in Partien von Vereinsspielern sind eines dieser drei.") },
                    { head: qsTr("Die Grundendspiele"),
                      body: qsTr("Dame und König gegen König, Turm und König gegen König, zwei Läufer, Läufer und Springer — vier Matts, die man können muss, weil sie in jeder Partie vorkommen können, die man gewonnen hat. Dazu die Opposition im Bauernendspiel und die Quadratregel für den Freibauern.") },
                    { head: qsTr("Notation"),
                      body: qsTr("Die Felder heißen a1 bis h8: Buchstabe für die Linie, Ziffer für die Reihe. Ein Zug wird als Figur plus Zielfeld geschrieben, Sf3 heißt Springer nach f3; ohne Figurenbuchstaben ist es ein Bauernzug. x steht für einen Schlag, + für Schach, # für Matt, 0-0 für die kurze und 0-0-0 für die lange Rochade.") }
                ]

                Column {
                    x: Style.horizontalPageMargin
                    width: page.width - 2 * Style.horizontalPageMargin
                    spacing: Style.paddingSmall / 2

                    Item { width: 1; height: Style.paddingMedium }

                    Label {
                        width: parent.width
                        text: modelData.head
                        color: Style.highlightColor
                        font.pixelSize: Style.fontSizeSmall
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        width: parent.width
                        text: modelData.body
                        color: Style.secondaryColor
                        font.pixelSize: Style.fontSizeExtraSmall
                        wrapMode: Text.WordWrap
                    }
                }
            }

            SectionHeader { text: qsTr("Die sechs Fertigkeiten") }

            Repeater {
                model: Dimensions.keys

                Column {
                    x: Style.horizontalPageMargin
                    width: page.width - 2 * Style.horizontalPageMargin
                    spacing: Style.paddingSmall / 2

                    Label {
                        width: parent.width
                        text: Dimensions.nameFor(modelData) + "  (" + modelData + ")"
                        color: Style.primaryColor
                        font.pixelSize: Style.fontSizeSmall
                    }
                    Label {
                        width: parent.width
                        text: Dimensions.questionFor(modelData)
                        color: Style.secondaryColor
                        font.pixelSize: Style.fontSizeExtraSmall
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
