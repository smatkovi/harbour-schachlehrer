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

// Online spielen (chess-spec/platform.md §3, docs/design.md §8 M8).
//
// Two things this page has to be honest about, and both are said in full
// sentences rather than hinted at:
//
//   * The account is optional. Everything the app is actually for — messen,
//     spielen, diagnostizieren, drillen — works without ever opening this
//     page. It says so where the login button is, not in the About text.
//   * While a game runs, the engine is off, and it is off because the game is
//     rated on somebody else's server. The rule is explained *before* the
//     first game, not as an error message during it.
Page {
    id: page
    objectName: "lichessPage"
    allowedOrientations: Orientation.All

    readonly property bool loggedIn: teacher.lichessLoggedIn
    readonly property bool authorising: teacher.lichessState === 1

    // The seek only reaches rapid, classical and correspondence (§3.2). Blitz
    // would need a direct challenge — and is the wrong format for learning.
    property var timeControls: [
        { label: qsTr("10 Minuten (Schnellschach)"), minutes: 10, increment: 0 },
        { label: qsTr("15 Minuten + 10 s"), minutes: 15, increment: 10 },
        { label: qsTr("30 Minuten (Turnierschach)"), minutes: 30, increment: 0 },
        { label: qsTr("60 Minuten"), minutes: 60, increment: 0 }
    ]
    property int timeControlIndex: 1
    property bool rated: false

    function openGame() {
        var top = pageStack.currentPage
        if (top && top.objectName === "onlineGamePage")
            return
        pageStack.push(Qt.resolvedUrl("OnlineGamePage.qml"))
    }

    Connections {
        target: teacher
        onOnlineGameChanged: {
            if (teacher.liveGame)
                page.openGame()
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        PullDownMenu {
            MenuItem {
                text: qsTr("Abmelden")
                visible: page.loggedIn
                onClicked: remorse.execute(qsTr("Abmelden"), function () { teacher.lichessLogOut() })
            }
            MenuItem {
                text: qsTr("Aktualisieren")
                visible: page.loggedIn
                onClicked: teacher.lichessRefresh()
            }
        }

        RemorsePopup { id: remorse }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: qsTr("Online spielen")
                description: page.loggedIn ? teacher.lichessAccount : qsTr("Lichess")
            }

            // ---- Die Regel, vor der ersten Partie ----------------------
            Item {
                width: parent.width
                height: rule.height + 2 * Style.paddingMedium

                Rectangle {
                    anchors.fill: parent
                    color: Style.highlightColor
                    opacity: 0.12
                    radius: Style.paddingSmall
                }

                Column {
                    id: rule
                    x: Style.horizontalPageMargin
                    y: Style.paddingMedium
                    width: parent.width - 2 * Style.horizontalPageMargin
                    spacing: Style.paddingSmall

                    Label {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        color: Style.primaryColor
                        font.pixelSize: Style.fontSizeSmall
                        text: qsTr("Während einer Lichess-Partie ist die Engine aus.")
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

            // ---- Anmelden ---------------------------------------------
            SectionHeader {
                text: qsTr("Konto")
                visible: !page.loggedIn
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: !page.loggedIn
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Du brauchst kein Lichess-Konto, um mit dieser App zu lernen. Der Einstufungstest, die Wiederholungen, der Blunder-Check und das Sparring gegen die eingebaute Engine funktionieren vollständig ohne Anmeldung. Ein Konto bringt zwei Dinge dazu: Partien gegen echte Gegner, und deine schon gespielten Partien als Material für die Fehlerdiagnose.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !page.loggedIn
                text: page.authorising ? qsTr("Anmeldeseite erneut öffnen")
                                       : qsTr("Mit Lichess anmelden")
                onClicked: {
                    teacher.lichessLogIn()
                    if (teacher.lichessAuthUrl !== "")
                        Qt.openUrlExternally(teacher.lichessAuthUrl)
                }
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: !page.loggedIn
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeTiny
                text: qsTr("Die Anmeldung läuft im Browser, damit du siehst, auf welcher Seite du dein Passwort eingibst. Wir bekommen nur einen Schlüssel, kein Passwort, und er liegt auf diesem Gerät in einer Datei, die nur diese App lesen kann.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: teacher.lichessMessage !== ""
                wrapMode: Text.WordWrap
                color: Style.highlightColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: teacher.lichessMessage
            }

            // ---- Angemeldet -------------------------------------------
            SectionHeader {
                text: qsTr("Angemeldet")
                visible: page.loggedIn
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.loggedIn
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: teacher.lichessConnected
                      ? qsTr("Verbunden als %1. Herausforderungen und Partien kommen von allein an.").arg(teacher.lichessAccount)
                      : qsTr("Angemeldet als %1. Die Verbindung zum Ereignisstrom wird gerade aufgebaut.").arg(teacher.lichessAccount)
            }

            // ---- Eine Partie suchen ------------------------------------
            SectionHeader {
                text: qsTr("Partie suchen")
                visible: page.loggedIn
            }

            ComboBox {
                width: parent.width
                visible: page.loggedIn
                label: qsTr("Bedenkzeit")
                description: qsTr("Über die Suchanzeige sind Schnellschach, Turnierschach und Fernschach erreichbar. Blitz nicht — und das ist kein Verlust, denn zum Lernen ist es das falsche Format.")
                currentIndex: page.timeControlIndex
                menu: ContextMenu {
                    Repeater {
                        model: page.timeControls
                        MenuItem { text: modelData.label }
                    }
                }
                onCurrentIndexChanged: page.timeControlIndex = currentIndex
            }

            TextSwitch {
                width: parent.width
                visible: page.loggedIn
                text: qsTr("Gewertet spielen")
                description: qsTr("Gewertete Partien verändern deine Lichess-Wertung. Für das Lernen macht es keinen Unterschied — die Fehler sind dieselben.")
                checked: page.rated
                onCheckedChanged: page.rated = checked
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.loggedIn
                text: teacher.lichessSeeking ? qsTr("Suche abbrechen") : qsTr("Gegner suchen")
                onClicked: {
                    if (teacher.lichessSeeking) {
                        teacher.lichessCancelSeek()
                    } else {
                        var control = page.timeControls[page.timeControlIndex]
                        teacher.lichessSeek(control.minutes, control.increment, page.rated)
                    }
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.loggedIn && !teacher.lichessSeeking
                text: qsTr("Fernschach, 3 Tage pro Zug")
                onClicked: teacher.lichessSeekCorrespondence(3, page.rated)
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.loggedIn && teacher.lichessSeeking
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Die Suche läuft. Sie bleibt nur so lange offen, wie die App läuft.")
            }

            // ---- Herausforderungen -------------------------------------
            SectionHeader {
                text: qsTr("Herausforderungen")
                visible: page.loggedIn && teacher.lichessChallenges.length > 0
            }

            Repeater {
                model: page.loggedIn ? teacher.lichessChallenges : []

                Column {
                    x: Style.horizontalPageMargin
                    width: page.width - 2 * Style.horizontalPageMargin
                    spacing: Style.paddingSmall / 2

                    Label {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        color: Style.primaryColor
                        font.pixelSize: Style.fontSizeSmall
                        text: modelData.text
                    }

                    Label {
                        width: parent.width
                        visible: !modelData.playable
                        wrapMode: Text.WordWrap
                        color: Style.errorColor
                        font.pixelSize: Style.fontSizeTiny
                        text: qsTr("Diese Partieform lässt sich mit dieser App nicht spielen.")
                    }

                    Row {
                        spacing: Style.paddingMedium
                        visible: modelData.incoming

                        Button {
                            text: qsTr("Annehmen")
                            enabled: modelData.playable
                            onClicked: teacher.lichessAcceptChallenge(modelData.id)
                        }
                        Button {
                            text: qsTr("Ablehnen")
                            onClicked: teacher.lichessDeclineChallenge(modelData.id)
                        }
                    }

                    Item { width: 1; height: Style.paddingSmall }
                }
            }

            // ---- Gegen den Lichess-Computer ----------------------------
            SectionHeader {
                text: qsTr("Gegen den Lichess-Computer")
                visible: page.loggedIn
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.loggedIn
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Auch dabei bleibt unsere eigene Engine aus: die Partie läuft auf dem Server von Lichess und wird dort nach denselben Regeln geprüft.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.loggedIn
                text: qsTr("Stufe 3, 15 Minuten")
                onClicked: teacher.lichessChallengeAi(3, 15, 0)
            }

            // ---- Die eigenen Partien holen -----------------------------
            SectionHeader {
                text: qsTr("Deine Partien")
                visible: page.loggedIn
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.loggedIn
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Deine schon gespielten Lichess-Partien kommen in dieselbe Datenbank wie deine Übungspartien. Aus ihnen zieht die Fehlerdiagnose ihr Material — echte Partien gegen echte Gegner sagen mehr über deine Fehler als jedes Sparring. Das erste Mal dauert es je nach Anzahl eine Weile, danach kommen nur die neuen dazu.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.loggedIn
                wrapMode: Text.WordWrap
                color: Style.highlightColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: {
                    var sync = teacher.gameSync
                    if (!sync)
                        return ""
                    if (sync.running)
                        return qsTr("%1 Partien gelesen, %2 übernommen.").arg(sync.seen).arg(sync.imported)
                    if (sync.message !== "")
                        return sync.message
                    return qsTr("%n Partie(n) von Lichess in der Datenbank.", "", sync.stored)
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.loggedIn
                enabled: !teacher.gameSync.running
                text: qsTr("Partien abgleichen")
                onClicked: teacher.lichessSyncGames()
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                // Hidden, not greyed out, while a game is running: an analysis
                // during a Lichess game is cheating (platform.md §3.7).
                visible: page.loggedIn && teacher.gameSync.hasLastGame && teacher.analysisAvailable
                text: qsTr("Letzte geholte Partie auswerten")
                onClicked: {
                    teacher.analyseSyncedGame()
                    pageStack.push(Qt.resolvedUrl("AnalysisPage.qml"))
                }
            }

            Item { width: 1; height: Style.paddingLarge }
        }
    }
}
