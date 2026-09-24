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

// The start page of docs/design.md §7: today's session, the six dimensions,
// and the way to the placement test. The loop of the method — messen, spielen,
// diagnostizieren, drillen, nachmessen — is what this page has to make
// obvious, so the session block comes first and the measurement last.
Page {
    id: page
    objectName: "mainPage"
    allowedOrientations: Orientation.All

    Component.onCompleted: Prefs.adopt(teacher)

    function toBoard() {
        pageStack.push(Qt.resolvedUrl("BoardPage.qml"))
    }

    // The three blocks of teacher.md §5.6. `session` is a map whose keys are
    // not nailed down, so each block asks for the ones it might be under and
    // falls back to saying nothing rather than to saying something wrong.
    function sessionValue(keys, fallback) {
        var s = teacher.session
        if (!s)
            return fallback
        for (var i = 0; i < keys.length; ++i) {
            if (s[keys[i]] !== undefined)
                return s[keys[i]]
        }
        return fallback
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        PullDownMenu {
            MenuItem {
                text: qsTr("Über Schachlehrer")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: qsTr("Einstellungen")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Regeln und Glossar")
                onClicked: pageStack.push(Qt.resolvedUrl("RulesPage.qml"))
            }
        }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: qsTr("Schachlehrer")
                description: teacher.dueCards > 0
                             ? qsTr("%n Wiederholungen fällig", "", teacher.dueCards)
                             : qsTr("Heute nichts fällig")
            }

            EngineBanner { }

            // ---- Die heutige Sitzung ----------------------------------
            SectionHeader { text: qsTr("Heutige Sitzung") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Fünfzehn Minuten in drei Blöcken: erst die fälligen Wiederholungen, dann ein neues Muster, dann gespielt. Gespielt wird immer.")
            }

            Repeater {
                model: [
                    { title: qsTr("A · Wiederholen"),
                      hint: qsTr("Die fälligen Karten, gemischt über die Dimensionen"),
                      keys: ["due", "blockA", "reviews", "dueCards"] },
                    { title: qsTr("B · Neues Muster"),
                      hint: qsTr("Ein Muster, fünf Stellungen hintereinander"),
                      keys: ["newPattern", "blockB", "pattern"] },
                    { title: qsTr("C · Spielen"),
                      hint: qsTr("Sparring mit Rücknahme und Erklärung"),
                      keys: ["play", "blockC", "sparring"] }
                ]

                Item {
                    width: page.width
                    height: block.height + Style.paddingSmall

                    Column {
                        id: block
                        x: Style.horizontalPageMargin
                        width: parent.width - 2 * Style.horizontalPageMargin
                        spacing: Style.paddingSmall / 2

                        Item {
                            width: parent.width
                            height: blockTitle.height
                            Label {
                                id: blockTitle
                                text: modelData.title
                                font.pixelSize: Style.fontSizeSmall
                                color: Style.primaryColor
                            }
                            Label {
                                anchors.right: parent.right
                                anchors.baseline: blockTitle.baseline
                                font.pixelSize: Style.fontSizeExtraSmall
                                color: Style.secondaryColor
                                text: {
                                    var v = page.sessionValue(modelData.keys, undefined)
                                    return (v === undefined || v === null) ? "" : ("" + v)
                                }
                            }
                        }
                        Label {
                            width: parent.width
                            text: modelData.hint
                            color: Style.secondaryColor
                            font.pixelSize: Style.fontSizeTiny
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                // While a Lichess game is running the app is in a different
                // state altogether (platform.md §3.7): the way back to it is
                // the only button that makes sense.
                visible: !teacher.liveGame
                text: qsTr("Sitzung beginnen")
                onClicked: {
                    teacher.startSession()
                    page.toBoard()
                }
            }

            // ---- Die unterbrochene Partie ------------------------------
            // Eine Sparringpartie überlebt das Schließen der App. Sie wird
            // nicht stillschweigend wieder aufgemacht: der Lernende sagt, ob
            // er weiterspielt oder neu anfängt.
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !teacher.liveGame && teacher.canResume
                text: qsTr("Partie fortsetzen")
                onClicked: {
                    teacher.resumeGame()
                    page.toBoard()
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !teacher.liveGame
                text: teacher.canResume ? qsTr("Neue Partie") : qsTr("Sparring")
                enabled: teacher.engineReady
                onClicked: {
                    // Handicap 0 is "spiel dein bestes"; the error budget of
                    // teacher.md §7 is set from the settings page.
                    teacher.startSparring(0)
                    page.toBoard()
                }
            }

            // ---- Die sechs Dimensionen --------------------------------
            SectionHeader { text: qsTr("Deine sechs Fertigkeiten") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                visible: !teacher.measured
                text: qsTr("Noch nichts gemessen. Der Einstufungstest dauert etwa zehn Minuten und sagt dir, womit du anfangen sollst.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: teacher.measured
                wrapMode: Text.WordWrap
                color: Style.highlightColor
                font.pixelSize: Style.fontSizeSmall
                text: qsTr("Gemessen am %1 — Aufgaben um %2.")
                      .arg(teacher.measuredOn).arg(teacher.startDifficulty)
            }

            Repeater {
                model: teacher.measured ? teacher.skills : 0
                SkillRow { entry: modelData }
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: teacher.measured
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Fünfundzwanzig Aufgaben reichen nicht, um einzelne Fertigkeiten sicher zu trennen. „Unauffällig“ heißt deshalb meistens: nichts stach heraus — nicht, dass nichts gemessen wurde.")
            }

            // ---- Messen ------------------------------------------------
            SectionHeader { text: qsTr("Messen") }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !teacher.liveGame
                text: qsTr("Einstufungstest")
                enabled: teacher.engineReady
                onClicked: pageStack.push(Qt.resolvedUrl("PlacementPage.qml"))
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeTiny
                text: qsTr("Fünfundzwanzig Aufgaben, die sich an dich anpassen. Sie geben eine grobe Einschätzung und die Reihenfolge, in der du üben solltest — keine Ratingzahl.")
            }

            Item { width: 1; height: Style.paddingMedium }

            // ---- Online spielen ----------------------------------------
            // M8. The protocol handling and the fair-play lock are covered by
            // test_lichess and test_fairplay; what no test can reach is the
            // round trip through the browser on a real device.
            readonly property bool onlineReady: true

            SectionHeader {
                text: qsTr("Online spielen")
                visible: content.onlineReady || teacher.liveGame
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                visible: content.onlineReady || teacher.liveGame
                text: teacher.liveGame
                      ? qsTr("Deine Lichess-Partie läuft. Solange sie läuft, ist die Engine aus — nach der Partie sehen wir sie uns an.")
                      : qsTr("Gegen echte Gegner auf Lichess spielen, und die eigenen Lichess-Partien als Material für die Fehlerdiagnose holen. Freiwillig: alles andere in dieser App funktioniert ohne Konto.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: content.onlineReady
                text: teacher.liveGame ? qsTr("Zurück zur laufenden Partie")
                                       : qsTr("Lichess")
                onClicked: {
                    if (teacher.liveGame)
                        pageStack.push(Qt.resolvedUrl("OnlineGamePage.qml"))
                    else
                        pageStack.push(Qt.resolvedUrl("LichessPage.qml"))
                }
            }

            // The way out, should the app ever get stuck in the online state.
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: teacher.liveGame
                text: qsTr("Online verlassen")
                onClicked: teacher.leaveOnline()
            }

            // ---- Der Rest ----------------------------------------------
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Karten")
                onClicked: pageStack.push(Qt.resolvedUrl("CardsPage.qml"))
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                // Hidden, not greyed out, while a Lichess game is running.
                visible: teacher.analysisAvailable
                text: qsTr("Letzte Partie auswerten")
                onClicked: pageStack.push(Qt.resolvedUrl("AnalysisPage.qml"))
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                // While a Lichess game runs, the board is that game's board and
                // belongs to OnlineGamePage; a second way in would only invite
                // moves into the wrong screen.
                visible: !teacher.liveGame
                text: qsTr("Freies Brett")
                onClicked: page.toBoard()
            }
        }
    }
}
