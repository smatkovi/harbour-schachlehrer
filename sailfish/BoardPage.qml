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

// The board that sparring and drill share (docs/design.md §7). The layout is
// the same in both modes on purpose: prompt above, board in the middle,
// feedback sentence below. What changes between a drill and a game is the text
// in those two strips, not the arrangement, so nothing has to be relearned.
//
// Portrait only. A board that has to compete with the keyboard-less landscape
// for height ends up with squares too small to hit, and the whole point of
// tap-tap entry is that the target square is visible while you aim at it.
Page {
    id: page
    objectName: "boardPage"
    allowedOrientations: Orientation.Portrait | Orientation.PortraitInverted

    readonly property bool sparring: teacher.mode === 3
    readonly property bool drill: teacher.mode === 2
    // §7.5: while the blunder check holds a move, the board is read-only —
    // the move is already chosen, the question is what he answers to it.
    readonly property bool checking: teacher.blunderCheck !== undefined
                                     && teacher.blunderCheck !== null
                                     && teacher.blunderCheck.active === true

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        PullDownMenu {
            MenuItem {
                text: qsTr("Partie auswerten")
                // platform.md §3.7: while a Lichess game runs this entry is
                // not disabled, it is not there.
                visible: teacher.analysisAvailable
                onClicked: {
                    teacher.analyseCurrentGame()
                    pageStack.push(Qt.resolvedUrl("AnalysisPage.qml"))
                }
            }
            MenuItem {
                text: qsTr("Aufgabe überspringen")
                visible: teacher.mode !== 0
                onClicked: teacher.skipTask()
            }
            MenuItem {
                text: qsTr("Brett drehen")
                onClicked: teacher.flipped = !teacher.flipped
            }
        }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: page.sparring ? qsTr("Sparring")
                       : page.drill ? qsTr("Übung")
                       : qsTr("Brett")
                description: teacher.gameResult !== "" ? teacher.gameResult
                             : (teacher.whiteToMove ? qsTr("Weiß am Zug")
                                                    : qsTr("Schwarz am Zug"))
            }

            EngineBanner { }

            // ---- Die Aufgabenstellung, über dem Brett ------------------
            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: text !== ""
                text: teacher.prompt
                color: Style.highlightColor
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeSmall
            }

            // Welcher Versuch gerade läuft. Ohne das steht dieselbe Stellung
            // wortlos ein zweites Mal da und man glaubt, der Zug sei gar nicht
            // angekommen.
            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.drill && (teacher.retrying || teacher.attempt === 2)
                text: teacher.retrying ? qsTr("Freier Durchgang — zählt nicht mehr")
                                       : qsTr("Zweiter Versuch")
                color: Style.secondaryColor
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeExtraSmall
            }

            // ---- Das Brett ---------------------------------------------
            Board {
                id: board
                anchors.horizontalCenter: parent.horizontalCenter
                width: page.width - 2 * Style.paddingSmall
                height: width
                // A Lichess game is played on OnlineGamePage, with its clocks
                // and its rules; this board does not take moves for it.
                // Eine schon beantwortete Aufgabe nimmt keinen Zug mehr an:
                // sonst tippt man auf dem Brett herum, auf dem man gerade
                // gescheitert ist, und nichts davon zaehlt.
                interactive: teacher.gameResult === "" && !teacher.thinking
                             && !page.checking && !teacher.liveGame
                             && !teacher.awaitingNext

                onMoveRejected: {
                    // A refused move is not an error message; it is simply not
                    // a move. Let go of the selection and say nothing.
                    teacher.selectedSquare = -1
                }
            }

            // ---- Der Blunder-Check, direkt unter dem Brett -------------
            // teacher.md §7.5. Er steht hier und nicht in einem Dialog, weil
            // die Antwort am Brett zu sehen ist und nicht im Text.
            BlunderCheckOverlay {
                width: parent.width
            }

            // ---- Wie lang die Antwort noch ist -------------------------
            // teacher.md §6.5. Nur bei Aufgaben über einen Zug hinaus, und es
            // steht nie da, *was* für eine Aufgabe es ist (§6.2).
            LinePanel {
                width: parent.width
                visible: !solution.active
            }

            // ---- Die Lösung, Halbzug für Halbzug -----------------------
            SolutionPanel {
                id: solution
                width: parent.width
            }

            // Die Frage nach dem zweiten Fehlversuch: Lösung, nochmal oder
            // weiter. Sie steht als drei Knöpfe da und nicht als Dialog — die
            // Stellung, um die es geht, soll dabei sichtbar bleiben.
            Column {
                width: parent.width
                spacing: Style.paddingMedium
                visible: page.drill && (!solution.active || teacher.awaitingNext)

                Label {
                    x: Style.horizontalPageMargin
                    width: parent.width - 2 * Style.horizontalPageMargin
                    visible: teacher.awaitingNext
                    wrapMode: Text.WordWrap
                    color: Style.secondaryColor
                    font.pixelSize: Style.fontSizeExtraSmall
                    // Die Karte ist gewertet, egal was jetzt kommt. Das gehört
                    // dazugesagt, sonst wirkt "Nochmal" wie eine zweite Chance
                    // auf den Termin.
                    text: qsTr("Die Aufgabe ist gezählt. Ein weiterer Versuch ändert daran nichts mehr.")
                }

                Flow {
                    width: parent.width - 2 * Style.horizontalPageMargin
                    x: Style.horizontalPageMargin
                    spacing: Style.paddingMedium

                    Button {
                        visible: !solution.active
                        text: qsTr("Lösung ansehen")
                        // §6.6 Hilfestufe 4. Bei einer **offenen** Aufgabe kostet
                        // sie die Aufgabe, und das steht auf dem Knopf und nicht im
                        // Kleingedruckten. War die Aufgabe dagegen schon
                        // beantwortet — nach dem zweiten Fehlversuch oder in einem
                        // freiwilligen Durchgang —, ist nichts mehr zu verlieren,
                        // dann wäre die Rückfrage eine Unwahrheit.
                        onClicked: {
                            if (teacher.awaitingNext || teacher.retrying)
                                teacher.showSolution()
                            else
                                solutionRemorse.execute(
                                    qsTr("Zeigen — die Aufgabe gilt dann als nicht gelöst"),
                                    function () { teacher.showSolution() })
                        }
                    }

                    Button {
                        // Dieselbe Stellung noch einmal, ohne Wertung: wer die
                        // Lösung nicht sehen will, sondern sie selbst finden.
                        visible: teacher.awaitingNext
                        text: qsTr("Nochmal")
                        onClicked: teacher.retryTask()
                    }

                    Button {
                        // Nach einer falschen Antwort bleibt die Aufgabe stehen,
                        // bis der Lernende weitergeht — vorher lud die nächste
                        // sofort nach und die verlorene war nicht mehr zu sehen.
                        visible: teacher.awaitingNext
                        text: qsTr("Weiter")
                        onClicked: teacher.continueDrill()
                    }
                }
            }

            RemorsePopup { id: solutionRemorse }

            // ---- Der Satz, unter dem Brett -----------------------------
            FeedbackPanel {
                id: feedback
                width: parent.width
            }

            ThinkingIndicator {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
            }

            // ---- Die Knöpfe --------------------------------------------
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Style.paddingMedium

                Button {
                    text: qsTr("Zurücknehmen")
                    // Free in sparring, and the engine explains what the taken
                    // back move did (docs/design.md §4, teacher.md §7).
                    enabled: teacher.mode !== 0 && !teacher.thinking
                    onClicked: teacher.takeBack()
                }

                Button {
                    text: qsTr("Hinweis")
                    // A hint during a rated Lichess game is exactly the
                    // "move recommendation from software" the fair-play rules
                    // forbid (§3.7), so it disappears with the engine. Und
                    // eine schon beantwortete Aufgabe braucht keinen mehr.
                    visible: !teacher.liveGame
                    enabled: teacher.engineReady && !teacher.thinking
                             && !teacher.awaitingNext
                    onClicked: teacher.requestHint()
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: teacher.gameResult !== "" && teacher.analysisAvailable
                text: qsTr("Partie auswerten")
                onClicked: {
                    teacher.analyseCurrentGame()
                    pageStack.push(Qt.resolvedUrl("AnalysisPage.qml"))
                }
            }

            // ---- Was frage ich mich? ------------------------------------
            // teacher.md §6.6: die übertragbaren Hinweise *sind* die Fragen.
            // Eingeklappt, weil eine Routine in den Kopf gehört und nicht an
            // die Wand.
            RoutinePanel {
                width: parent.width
            }

            // ---- Die Zugliste -------------------------------------------
            SectionHeader {
                text: qsTr("Züge")
                visible: teacher.moveList && teacher.moveList.length > 0
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: teacher.moveList && teacher.moveList.length > 0
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: MoveList.asText(teacher.moveList)
            }
        }
    }
}
