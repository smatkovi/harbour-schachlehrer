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

// Die laufende Lichess-Partie (chess-spec/platform.md §3.5).
//
// The layout is the board page's, on purpose — nothing has to be relearned.
// Three things are different, and all three follow from §3.7:
//
//   * There is no hint button, no evaluation, no analysis entry. Not greyed
//     out: **not there**.
//   * In their place stands a sentence that says why. Silence would look like
//     a bug, and a user who thinks the engine is broken is a user who goes
//     looking for another one — which is exactly the thing that gets accounts
//     marked.
//   * The clocks are the server's. Ours only interpolate between two
//     `gameState` lines; the next line corrects them.
Page {
    id: page
    objectName: "onlineGamePage"
    orientationLock: PageOrientation.Automatic

    property variant game: teacher.onlineGame
    property variant clock: teacher.clocks
    property bool live: teacher.liveGame
    property bool over: !teacher.liveGame && page.game.valid

    // Coming back from the background mid-game: the connection may have been
    // dropped while nothing was drawing (§3.5).
    Connections {
        target: Qt.application
        ignoreUnknownSignals: true
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationActive)
                teacher.appActivated()
        }
    }

    Flickable {
        anchors.fill: parent
        pressDelay: 150
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        PullDownMenu {
            MenuItem {
                text: qsTr("Brett drehen")
                onClicked: teacher.flipped = !teacher.flipped
            }
            MenuItem {
                text: qsTr("Partie abbrechen")
                // Only possible before both sides have moved (§3.2).
                visible: page.live && page.game.moveCount < 2
                onClicked: teacher.lichessAbort()
            }
            MenuItem {
                text: qsTr("Aufgeben")
                visible: page.live && page.game.moveCount >= 2
                onClicked: resignRemorse.execute(qsTr("Aufgeben"),
                                                 function () { teacher.lichessResign() })
            }
            // No "Partie auswerten" while the game runs. Hidden, not disabled
            // (platform.md §3.7).
            MenuItem {
                text: qsTr("Partie auswerten")
                visible: teacher.analysisAvailable && page.over
                onClicked: {
                    teacher.analyseCurrentGame()
                    pageStack.push(Qt.resolvedUrl("AnalysisPage.qml"))
                }
            }
        }

        RemorsePopup { id: resignRemorse }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: page.game.opponent !== "" ? page.game.opponent : qsTr("Lichess")
                description: teacher.gameResult !== ""
                             ? teacher.gameResult
                             : (page.game.ourTurn ? qsTr("Du bist am Zug")
                                                  : qsTr("Er ist am Zug"))
            }

            // ---- Die Uhren ---------------------------------------------
            Item {
                width: parent.width
                height: clocks.height
                visible: page.clock.visible

                Row {
                    id: clocks
                    x: Style.horizontalPageMargin
                    width: parent.width - 2 * Style.horizontalPageMargin
                    spacing: Style.paddingLarge

                    Column {
                        width: (parent.width - Style.paddingLarge) / 2
                        Label {
                            text: qsTr("Er")
                            color: Style.secondaryColor
                            font.pixelSize: Style.fontSizeTiny
                        }
                        Label {
                            text: page.clock.theirs
                            color: page.game.ourTurn ? Style.secondaryColor : Style.primaryColor
                            font.pixelSize: Style.fontSizeLarge
                        }
                    }

                    Column {
                        width: (parent.width - Style.paddingLarge) / 2
                        Label {
                            text: qsTr("Du")
                            color: Style.secondaryColor
                            font.pixelSize: Style.fontSizeTiny
                        }
                        Label {
                            text: page.clock.ours
                            color: page.clock.lowOnTime && page.game.ourTurn
                                   ? Style.errorColor
                                   : (page.game.ourTurn ? Style.primaryColor : Style.secondaryColor)
                            font.pixelSize: Style.fontSizeLarge
                        }
                    }
                }
            }

            // ---- Warum die Engine aus ist -------------------------------
            // The rule, visible instead of silent. It stays on screen for the
            // whole game — this is the one place where repeating ourselves is
            // cheaper than being misunderstood.
            Item {
                width: parent.width
                height: page.live ? notice.height + 2 * Style.paddingMedium : 0
                visible: page.live

                Rectangle {
                    anchors.fill: parent
                    color: Style.highlightColor
                    opacity: 0.12
                    radius: Style.paddingSmall
                }

                Column {
                    id: notice
                    x: Style.horizontalPageMargin
                    y: Style.paddingMedium
                    width: parent.width - 2 * Style.horizontalPageMargin
                    spacing: Style.paddingSmall

                    Label {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        color: Style.primaryColor
                        font.pixelSize: Style.fontSizeSmall
                        text: qsTr("Die Engine ist aus - die Partie wird auf dem Server von Lichess gewertet.")
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        color: Style.secondaryColor
                        font.pixelSize: Style.fontSizeExtraSmall
                        text: teacher.fairPlayNotice
                    }
                }
            }

            // ---- Das Brett ----------------------------------------------
            Board {
                id: board
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(page.width, page.height) - 2 * Style.paddingSmall
                height: width
                // §3.5 step 5: the move goes to the server and the board waits
                // for the next `gameState`. Tapping again in between does no
                // harm; the server decides.
                interactive: page.live && page.game.ourTurn

                onMoveRejected: teacher.selectedSquare = -1
            }

            // A board that does not react must say why, or it reads as broken.
            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: text !== ""
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: {
                    if (!teacher.lichessLoggedIn)
                        return qsTr("Nicht angemeldet - ohne Konto laesst sich hier nicht ziehen.")
                    if (!page.live)
                        return qsTr("Gerade laeuft keine Partie. Such dir auf der Lichess-Seite eine.")
                    if (page.game.finished)
                        return qsTr("Die Partie ist vorbei.")
                    if (!page.game.ourTurn)
                        return qsTr("Dein Gegner ist am Zug.")
                    return ""
                }
            }

            // ---- Was gerade zu entscheiden ist ---------------------------
            FeedbackPanel {
                width: parent.width
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.live && page.game.millisToMove > 0 && page.game.moveCount < 2
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Fuer den ersten Zug gibt es eine Frist. Zieh bald, sonst bricht Lichess die Partie ab.")
            }

            // ---- Remis, Zurücknahme, Aufgabe -----------------------------
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Style.paddingMedium
                visible: page.live && page.game.drawOffered

                Button {
                    text: qsTr("Remis annehmen")
                    onClicked: teacher.lichessAnswerDraw(true)
                }
                Button {
                    text: qsTr("Weiterspielen")
                    onClicked: teacher.lichessAnswerDraw(false)
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Style.paddingMedium
                visible: page.live && page.game.takebackAsked

                Button {
                    text: qsTr("Zuruecknahme erlauben")
                    onClicked: teacher.lichessAnswerTakeback(true)
                }
                Button {
                    text: qsTr("Ablehnen")
                    onClicked: teacher.lichessAnswerTakeback(false)
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.live && page.game.opponentGone && page.game.claimWinInSeconds === 0
                text: qsTr("Sieg beanspruchen")
                onClicked: teacher.lichessClaimVictory()
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Style.paddingMedium
                visible: page.live && !page.game.drawOffered

                Button {
                    text: qsTr("Remis anbieten")
                    enabled: !page.game.drawOfferedByUs && page.game.moveCount >= 2
                    onClicked: teacher.lichessOfferDraw()
                }
                Button {
                    text: qsTr("Zuruecknahme bitten")
                    enabled: page.game.moveCount >= 2
                    onClicked: teacher.lichessRequestTakeback()
                }
            }

            // ---- Nach der Partie -----------------------------------------
            // Now it is allowed, and now it is where the learning happens
            // (platform.md §3.7: forbidden during, fine afterwards).
            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.over
                wrapMode: Text.WordWrap
                color: Style.highlightColor
                font.pixelSize: Style.fontSizeSmall
                text: qsTr("Die Partie ist vorbei - jetzt darf die Engine wieder mitreden. Lass sie durchsehen: Aus deinen eigenen Fehlern werden die Uebungskarten, mit denen du weiterkommst.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.over && teacher.analysisAvailable
                text: qsTr("Partie auswerten")
                onClicked: {
                    teacher.analyseCurrentGame()
                    pageStack.push(Qt.resolvedUrl("AnalysisPage.qml"))
                }
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.over && !teacher.analysisAvailable
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Ohne Engine kann ich die Partie nicht durchsehen. Sie ist gespeichert und wartet.")
            }

            // ---- Die Zugliste --------------------------------------------
            SectionHeader {
                text: qsTr("Zuege")
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
