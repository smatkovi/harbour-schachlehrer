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
    orientationLock: PageOrientation.Automatic

    Component.onCompleted: Prefs.adopt(teacher)

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
                text: qsTr("Moegliche Zielfelder markieren")
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
                text: qsTr("Zahlen zusaetzlich anzeigen")
                description: qsTr("Die Erklaerung im Klartext steht immer da. Zusaetzlich die Bewertung in Bauerneinheiten und die Gewinnaussicht in Prozent - hilfreich, sobald du weisst, was diese Zahlen nicht bedeuten.")
                checked: Prefs.showNumbers
                onCheckedChanged: Prefs.showNumbers = checked
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Eine Bewertungszahl ohne Satz gibt es in dieser App nicht. '+1,4' sagt nicht, was zu tun ist; 'Dein Laeufer steht ungedeckt und der Springer greift ihn an' sagt es.")
            }

            SectionHeader { text: qsTr("Blunder-Check") }

            // teacher.md §7.5. The schedule is the method's decision, not the
            // learner's — but forcing it on or off is legitimate: the one who
            // wants the scaffolding longer should get it, and the one who is
            // done with it should not have to fail three times to lose it.
            ComboBox {
                width: parent.width
                label: qsTr("Vor der Zugfreigabe fragen")
                description: qsTr("Im Sparring zeigt die App, was er als Schach oder Schlag zur Verfuegung hat, und du tippst an, was davon dich etwas kostet. Von allein wird die Abfrage seltener, je oefter du richtig liegst - und kommt zurueck, sobald wieder etwas haengen bleibt.")
                currentIndex: teacher.drillMode
                menu: ContextMenu {
                    MenuItem { text: qsTr("Wenn noetig") }
                    MenuItem { text: qsTr("Immer") }
                    MenuItem { text: qsTr("Nie") }
                }
                onCurrentIndexChanged: {
                    if (teacher.drillMode !== currentIndex)
                        teacher.drillMode = currentIndex
                }
            }

            SectionHeader { text: qsTr("Engine") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: teacher.engineReady ? Style.secondaryColor
                       : (teacher.liveGame ? Style.highlightColor : Style.errorColor)
                text: teacher.engineReady
                      ? qsTr("Die Engine laeuft. Sparring, Einstufung und Auswertung stehen zur Verfuegung.")
                      : (teacher.liveGame
                         ? qsTr("Die Engine ist aus, weil gerade eine Lichess-Partie laeuft. Das ist Absicht.")
                         : qsTr("Die Engine laeuft nicht. Brett, Karten und Regeln funktionieren trotzdem."))
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Stockfish laeuft als eigenes Programm neben der App und wird ueber UCI angesprochen. Die Endspieldatenbanken fuer drei und vier Steine sind im Paket enthalten; es wird nichts nachgeladen.")
            }

            SectionHeader { text: qsTr("Nachschub an Aufgaben") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                // teacher.md §5.2 is the reason this exists, and it is worth
                // saying: a position that comes back measures whether you
                // remember it, not whether you can do it.
                text: qsTr("%1 Uebungsstellungen sind gerade in Gebrauch, %2 liegen geholt bereit. Eine Stellung, die wiederkommt, misst nur noch, ob du sie kennst - deshalb ist Nachschub etwas wert. Geholte kommen beim naechsten Start dazu, nicht mitten in einer Sitzung.")
                      .arg(teacher.itemCount).arg(teacher.feedCount)
            }

            TextSwitch {
                text: qsTr("Neue Aufgaben von Lichess holen")
                description: qsTr("Beim Start, wenn eine Verbindung da ist. Ohne das laeuft alles weiter - die App bringt ihre Aufgaben mit und braucht dafuer weder Netz noch Konto.")
                checked: teacher.feedAllowed
                onClicked: teacher.feedAllowed = !teacher.feedAllowed
            }

            Slider {
                visible: teacher.feedAllowed
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                minimumValue: 100
                maximumValue: 2000
                stepSize: 100
                value: teacher.feedTarget
                label: qsTr("Hoechstens %1 geholte Aufgaben").arg(Math.round(value))
                valueText: Math.round(value)
                onPressedChanged: if (!pressed) teacher.feedTarget = Math.round(value)
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: teacher.feedMessage !== "" || teacher.feedBusy
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: teacher.feedBusy ? qsTr("Ich hole gerade welche ...") : teacher.feedMessage
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Style.paddingMedium
                visible: teacher.feedAllowed

                Button {
                    text: qsTr("Jetzt holen")
                    enabled: !teacher.feedBusy
                    onClicked: teacher.fetchPuzzles()
                }
                Button {
                    text: qsTr("Geholte loeschen")
                    enabled: teacher.feedCount > 0
                    onClicked: feedRemorse.execute(qsTr("Loeschen"),
                                                   function () { teacher.clearFetchedPuzzles() })
                }
            }

            RemorsePopup { id: feedRemorse }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                // The honest version of where they come from, and of what the
                // app does *not* do: the 304 MB database export stays on the
                // build host, and the phone asks for fifty at a time.
                text: qsTr("Geholt wird ueber die offene Lichess-Schnittstelle, fuenfzig Stueck je Anfrage und ohne Konto. Die vollstaendige Aufgabendatenbank ist 304 MB gross und wird nie auf das Telefon geladen; was die App mitbringt, ist eine daraus gebaute Auswahl.")
            }

            SectionHeader { text: qsTr("Lichess") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                text: teacher.lichessLoggedIn
                      ? qsTr("Angemeldet als %1.").arg(teacher.lichessAccount)
                      : qsTr("Nicht angemeldet. Das ist kein Mangel: Einstufung, Wiederholungen, Drill und Sparring brauchen kein Konto.")
            }

            // platform.md §3.7, said once more where the account is managed —
            // this is the rule that decides how the whole online part behaves.
            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: teacher.fairPlayNotice
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                // platform.md §3.3 allows two places for the key and the app
                // takes the better one it can get. Which one it got is the
                // user's business, so the sentence says it rather than
                // describing the good case and hoping.
                text: teacher.lichessKeyEncrypted
                      ? qsTr("Der Zugangsschluessel liegt %1 - nicht in den Einstellungen und nicht im Klartext irgendwo sonst. Beim Abmelden wird er geloescht und bei Lichess widerrufen.").arg(teacher.lichessKeyStore)
                      : qsTr("Der Schluesselspeicher von Sailfish OS antwortet nicht, deshalb liegt der Zugangsschluessel %1 im Datenverzeichnis dieser App - nicht in den Einstellungen und nicht im Klartext irgendwo sonst. Beim Abmelden wird er geloescht und bei Lichess widerrufen.").arg(teacher.lichessKeyStore)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: teacher.lichessLoggedIn ? qsTr("Von Lichess abmelden")
                                              : qsTr("Mit Lichess anmelden")
                onClicked: {
                    if (teacher.lichessLoggedIn)
                        lichessRemorse.execute(qsTr("Abmelden"),
                                               function () { teacher.lichessLogOut() })
                    else
                        pageStack.push(Qt.resolvedUrl("LichessPage.qml"))
                }
            }

            RemorsePopup { id: lichessRemorse }

            SectionHeader { text: qsTr("Zuruecksetzen") }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Darstellung zuruecksetzen")
                onClicked: {
                    Prefs.showNumbers = false
                    Prefs.showCoordinates = true
                    Prefs.showLegalTargets = true
                    Prefs.markLastMove = true
                    teacher.flipped = false
                    teacher.drillMode = 0
                }
            }
        }
    }
}
