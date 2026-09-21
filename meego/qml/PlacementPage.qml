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

// The adaptive placement test (chess-spec/teacher.md §4): six anchor items,
// then adaptively chosen ones, about 25 in total. What it produces is an
// ordinal verdict per dimension and an order to train in — not a rating, and
// the page says so instead of showing a number it cannot justify (§4.6).
Page {
    id: page
    objectName: "placementPage"
    orientationLock: PageOrientation.Automatic

    property bool started: false

    function begin() {
        page.started = true
        teacher.startPlacement()
    }

    Flickable {
        anchors.fill: parent
        pressDelay: 150
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        PullDownMenu {
            MenuItem {
                text: qsTr("Aufgabe ueberspringen")
                visible: page.started
                onClicked: teacher.skipTask()
            }
            MenuItem {
                text: qsTr("Brett drehen")
                visible: page.started
                onClicked: teacher.flipped = !teacher.flipped
            }
        }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: qsTr("Einstufung")
                description: teacher.reviewing ? qsTr("Loesungen")
                                               : (page.started ? qsTr("Aufgabe %1").arg(page.taskNumber())
                                                               : qsTr("etwa zehn Minuten"))
            }

            EngineBanner { }

            // ---- Vor dem Start ------------------------------------------
            Column {
                width: parent.width
                spacing: Style.paddingMedium
                visible: !page.started

                Label {
                    x: Style.horizontalPageMargin
                    width: parent.width - 2 * Style.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Style.secondaryColor
                    text: qsTr("Fuenfundzwanzig Stellungen. Jede ist so gewaehlt, dass du sie mit etwa siebzig Prozent Wahrscheinlichkeit loest - zu leichte und zu schwere Aufgaben sagen nichts ueber dich aus.")
                }
                Label {
                    x: Style.horizontalPageMargin
                    width: parent.width - 2 * Style.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Style.secondaryColor
                    text: qsTr("Am Ende steht keine Ratingzahl. Der Test sagt dir, welche zwei deiner sechs Fertigkeiten zuerst drankommen, und mehr gibt er ehrlicherweise nicht her.")
                }
                Label {
                    x: Style.horizontalPageMargin
                    width: parent.width - 2 * Style.horizontalPageMargin
                    wrapMode: Text.WordWrap
                    color: Style.secondaryColor
                    font.pixelSize: Style.fontSizeExtraSmall
                    text: qsTr("Es gibt keinen Hinweis und keine Ruecknahme. Wenn du eine Stellung nicht siehst, ueberspringe sie - das ist eine gueltige Antwort. Die Loesung kannst du dir danach jederzeit ansehen.")
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Test beginnen")
                    enabled: teacher.engineReady
                    onClicked: page.begin()
                }
            }

            // ---- Der Test ------------------------------------------------
            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: page.started && text !== ""
                text: teacher.prompt
                color: Style.highlightColor
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeSmall
            }

            Board {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.started
                width: Math.min(page.width, page.height) - 2 * Style.paddingSmall
                height: visible ? width : 0
                interactive: page.started && !teacher.thinking && !teacher.reviewing
                // No hint marks during a measurement: the legal-target dots
                // would turn a calculation task into a multiple choice.
                showLegalTargets: false
                onMoveRejected: teacher.selectedSquare = -1
            }

            // §6.5: on a task that is longer than one move the learner enters
            // the whole line, the opponent's replies included, and this says
            // how many are left — a count, never what sort of task it is.
            LinePanel {
                width: parent.width
                visible: !solution.active
            }

            // Die Lösung, Halbzug für Halbzug. Im Test nur *nach* der Antwort
            // erreichbar — §4.1 misst, was der Lernende ungestützt sieht.
            SolutionPanel {
                id: solution
                width: parent.width
            }

            FeedbackPanel {
                width: parent.width
                visible: page.started
            }

            ThinkingIndicator {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                label: qsTr("Wird ausgewertet ...")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.started && !teacher.reviewing
                text: qsTr("Ueberspringen")
                onClicked: teacher.skipTask()
            }

            // ---- Lösungen durchsehen -------------------------------------
            // A measurement you cannot look back at teaches nothing. The
            // buttons appear as soon as there is something to look at.
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Style.paddingMedium
                visible: page.started && teacher.reviewCount > 0

                Button {
                    text: teacher.reviewing ? qsTr("Zurueck") : qsTr("Loesung ansehen")
                    enabled: !teacher.reviewing || teacher.reviewIndex > 0
                    onClicked: teacher.reviewPrevious()
                }
                Button {
                    visible: teacher.reviewing
                    text: teacher.reviewIndex + 1 < teacher.reviewCount ? qsTr("Weiter")
                                                                        : qsTr("Zum Test")
                    onClicked: teacher.reviewNext()
                }
                Button {
                    visible: teacher.reviewing
                    text: qsTr("Weitermachen")
                    onClicked: teacher.hideSolution()
                }
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: teacher.reviewing
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                // It used to say "der markierte Zug ist die Lösung", and
                // nothing was marked: the board was set from the FEN, so it
                // had no last move to highlight. Now the line is stepped
                // through, and the text says what is actually there.
                text: qsTr("Aufgabe %1 von %2  mit den Pfeilen durch die Loesung")
                      .arg(teacher.review && teacher.review.number ? teacher.review.number : 0)
                      .arg(teacher.reviewCount)
            }

            // ---- Das Ergebnis --------------------------------------------
            SectionHeader {
                text: qsTr("Ergebnis")
                visible: page.started && teacher.skills && teacher.skills.length > 0
            }

            Repeater {
                model: page.started ? teacher.skills : 0
                SkillRow { entry: modelData }
            }
        }
    }

    // `task` is a map of unfixed shape; the index is nice to have and its
    // absence must not show as "Aufgabe undefined".
    function taskNumber() {
        var t = teacher.task
        if (!t)
            return ""
        if (t.index !== undefined) return "" + Number(t.index)
        if (t.number !== undefined) return "" + t.number
        if (t.n !== undefined) return "" + t.n
        return ""
    }
}
