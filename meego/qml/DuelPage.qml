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

// Eine Partie gegen ein zweites Gerät: im selben WLAN oder über Bluetooth.
// Ein Gerät eröffnet, das andere tritt bei; wer eröffnet, ist über beide Wege
// zugleich erreichbar und der Gast sucht sich aus, welchen er nimmt.
Page {
    id: page
    objectName: "duelPage"
    orientationLock: PageOrientation.Automatic

    property int colour: 0      // 0 Weiß, 1 Schwarz, 2 Zufall

    Component.onCompleted: {
        teacher.duel.browser.search()
        teacher.duel.bluetooth.refresh()
    }

    // Sobald die Partie steht, gehört der Bildschirm dem Brett.
    Connections {
        target: teacher
        onDuelChanged: {
            if (teacher.duel.playing && pageStack.currentPage === page)
                pageStack.replace(Qt.resolvedUrl("BoardPage.qml"))
        }
    }

    Flickable {
        anchors.fill: parent
        pressDelay: 150
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        PullDownMenu {
            MenuItem {
                text: qsTr("Erneut suchen")
                onClicked: {
                    teacher.duel.browser.search()
                    teacher.duel.bluetooth.refresh()
                }
            }
        }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader {
                title: qsTr("Zweites Geraet")
                description: qsTr("Gegen jemanden am anderen Geraet spielen")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeExtraSmall
                color: Style.secondaryColor
                text: qsTr("Auf beiden Geraeten muss der Schachlehrer laufen. Ueber WLAN muessen "
                           + "beide im selben Netz sein; ohne Netz geht es über Bluetooth, dafür "
                           + "die Geräte einmal in den Systemeinstellungen koppeln. Solange die "
                           + "Partie läuft, ist die Maschine aus — gegen einen Menschen zu "
                           + "spielen und sich ansagen zu lassen wäre kein Spiel.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: text !== ""
                wrapMode: Text.WordWrap
                color: Style.highlightColor
                text: teacher.duel.status
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: teacher.duel.role !== 0
                text: qsTr("Abbrechen")
                onClicked: teacher.leaveDuel()
            }

            // ---- Eröffnen ------------------------------------------------
            SectionHeader { text: qsTr("Tisch eroeffnen") }

            ComboBox {
                width: parent.width
                label: qsTr("Du spielst")
                enabled: teacher.duel.role === 0
                currentIndex: page.colour
                menu: ContextMenu {
                    MenuItem { text: qsTr("Weiss") }
                    MenuItem { text: qsTr("Schwarz") }
                    MenuItem { text: qsTr("Zufall") }
                }
                onCurrentIndexChanged: page.colour = currentIndex
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: teacher.duel.role === 0
                text: qsTr("Eroeffnen")
                onClicked: teacher.hostDuel(page.colour)
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeExtraSmall
                color: Style.secondaryColor
                text: (teacher.duel.browser.localAddresses !== ""
                       ? qsTr("Dieses Geraet im Netz: %1").arg(teacher.duel.browser.localAddresses)
                       : qsTr("Dieses Geraet haengt in keinem Netz"))
                      + "\n"
                      + (teacher.duel.bluetooth.available
                         ? qsTr("Bluetooth: %1").arg(teacher.duel.bluetooth.localName !== ""
                                ? teacher.duel.bluetooth.localName
                                : teacher.duel.bluetooth.localAddress)
                         : qsTr("Bluetooth ist ausgeschaltet"))
            }

            // ---- Beitreten -----------------------------------------------
            SectionHeader { text: qsTr("Im WLAN beitreten") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: teacher.duel.browser.hosts.length === 0
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeSmall
                color: Style.secondaryColor
                text: teacher.duel.browser.searching
                      ? qsTr("Suche laeuft ...")
                      : qsTr("Noch kein offener Tisch gefunden. Erst am anderen Geraet eroeffnen, "
                             + "dann hier von oben nachziehen.")
            }

            Repeater {
                model: teacher.duel.browser.hosts
                BackgroundItem {
                    width: content.width
                    height: AppTheme.itemSizeSmall
                    enabled: teacher.duel.role === 0
                    onClicked: teacher.joinDuel(modelData.address)

                    Column {
                        x: Style.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            text: modelData.name
                            color: parent.parent.highlighted ? Style.highlightColor : Style.primaryColor
                        }
                        Label {
                            text: modelData.address
                            font.pixelSize: Style.fontSizeExtraSmall
                            color: Style.secondaryColor
                        }
                    }
                }
            }

            SectionHeader { text: qsTr("Ueber Bluetooth beitreten") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                visible: teacher.duel.bluetooth.devices.length === 0
                wrapMode: Text.WordWrap
                font.pixelSize: Style.fontSizeSmall
                color: Style.secondaryColor
                text: teacher.duel.bluetooth.available
                      ? qsTr("Keine gekoppelten Geraete. Die beiden Geraete einmal in den "
                             + "Systemeinstellungen koppeln, dann hier von oben nachziehen.")
                      : qsTr("Bluetooth ist ausgeschaltet.")
            }

            Repeater {
                model: teacher.duel.bluetooth.devices
                BackgroundItem {
                    width: content.width
                    height: AppTheme.itemSizeSmall
                    enabled: teacher.duel.role === 0
                    onClicked: teacher.joinDuelBluetooth(modelData.address)

                    Column {
                        x: Style.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            text: modelData.name
                            color: parent.parent.highlighted ? Style.highlightColor : Style.primaryColor
                        }
                        Label {
                            text: modelData.address
                            font.pixelSize: Style.fontSizeExtraSmall
                            color: Style.secondaryColor
                        }
                    }
                }
            }

            SectionHeader { text: qsTr("Adresse eingeben") }

            TextField {
                id: manual
                width: parent.width
                label: qsTr("Adresse des eroeffnenden Geraets")
                placeholderText: qsTr("z. B. 192.168.1.23")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.enabled: text.trim().length > 0 && teacher.duel.role === 0
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    focus = false
                    teacher.joinDuel(text)
                }
            }
        }
    }
}
