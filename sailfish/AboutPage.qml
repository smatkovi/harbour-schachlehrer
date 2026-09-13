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

Page {
    id: page
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Style.paddingLarge
        VerticalScrollDecorator { }

        Column {
            id: content
            width: page.width
            spacing: Style.paddingMedium

            PageHeader { title: qsTr("Über Schachlehrer") }

            Image {
                width: Theme.itemSizeHuge
                height: width
                anchors.horizontalCenter: parent.horizontalCenter
                source: Qt.resolvedUrl("icons/icon-256.png")
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "Schachlehrer"
                font.pixelSize: Theme.fontSizeHuge
                font.bold: true
                color: Style.highlightColor
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Style.secondaryHighlightColor
                text: qsTr("Messen, spielen, die eigenen Fehler finden, genau die üben, nachmessen.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Version %1 · GPL-3.0-or-later · Copyright 2026 smatkovi").arg("0.1.1")
            }

            SectionHeader { text: qsTr("Was diese App anders macht") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                text: qsTr("Sie folgt keinem festen Lehrplan. Sie misst, was du kannst, lässt dich spielen, rechnet deine Partie nach, ordnet jeden Fehler einer von fünfzig Klassen zu und macht aus den meisten davon eine Übungskarte. Geübt wird, was dich tatsächlich Partien kostet.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                text: qsTr("Und: keine Bewertungszahl ohne Satz. Eine Zahl sagt dir, dass etwas schiefging, aber nicht was — deshalb steht hier immer erst der Satz.")
            }

            SectionHeader { text: qsTr("Engine") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Stockfish 17.1, GPL-3.0-or-later, als eigenes Programm mitgeliefert und über UCI angesprochen. Quelltext und Patch liegen unter /usr/share/licenses/harbour-schachlehrer.")
            }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Syzygy-Endspieldatenbanken für drei und vier Steine, 70 Dateien, gemeinfrei. Sie machen jedes Grundendspiel exakt benotbar, ohne Netzverbindung.")
            }

            SectionHeader { text: qsTr("Figuren") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Die Figuren sind der Satz „cburnett“ von Colin M. L. Burnett aus Wikimedia Commons, hier unter BSD-3-Clause verwendet. Alle Herkunftsangaben stehen in CREDITS/ASSETS.md, mitgeliefert unter /usr/share/doc/harbour-schachlehrer.")
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Style.paddingSmall

                Repeater {
                    model: ["wk", "wq", "wr", "wb", "wn", "wp"]
                    Piece {
                        width: Style.itemSizeSmall
                        height: width
                        piece: modelData
                    }
                }
            }

            SectionHeader { text: qsTr("Schachmodell") }

            Label {
                x: Style.horizontalPageMargin
                width: parent.width - 2 * Style.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Style.secondaryColor
                font.pixelSize: Style.fontSizeExtraSmall
                text: qsTr("Zuggenerierung und Regelwerk kommen aus Disservin/chess-library (MIT), auf perft-Stellungen geprüft.")
            }
        }
    }
}
