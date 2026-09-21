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

// The one place that decides how an evaluation is shown.
//
// docs/design.md §4: *keine Bewertungszahl ohne Satz*. `feedback` always
// carries `text`; `cp` and `wp` are optional and secondary, and at the lower
// learning levels they are not shown at all (Prefs.showNumbers). Putting the
// rule in one component is the only way it survives the next ten screens.
Item {
    id: panel

    // The QVariantMap from TeacherEngine.feedback, or any map shaped like it.
    property variant feedback: teacher.feedback

    property string sentence: (feedback && feedback.text) ? ("" + feedback.text) : ""
    property bool hasSentence: sentence !== ""
    property bool hasNumber: feedback !== undefined && feedback !== null
                                      && (feedback.cp !== undefined || feedback.wp !== undefined)

    // The rule the sentence came from (docs/design.md §4). Shown only where it
    // helps the learner look something up, never instead of the sentence.
    property bool showRuleKey: false

    // teacher.md §6.6, the link back: which question of the learner's own
    // routine would have caught this. It is *added* to the sentence above and
    // never replaces it, and it is empty when the error class has no question.
    property string questionSentence: (feedback && feedback.question
                                                && feedback.question.sentence)
                                               ? ("" + feedback.question.sentence) : ""

    // "verloren"/"gewonnen" colouring stays out of it on purpose: a sentence
    // that needs a colour to be understood is the wrong sentence.
    property color sentenceColor: Style.primaryColor

    width: parent ? parent.width : 0
    height: hasSentence ? column.height : 0
    visible: hasSentence

    Column {
        id: column
        width: parent.width
        spacing: Style.paddingSmall

        Label {
            x: Style.horizontalPageMargin
            width: parent.width - 2 * Style.horizontalPageMargin
            text: panel.sentence
            color: panel.sentenceColor
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeSmall
        }

        Label {
            x: Style.horizontalPageMargin
            width: parent.width - 2 * Style.horizontalPageMargin
            visible: panel.questionSentence !== ""
            text: panel.questionSentence
            color: Style.highlightColor
            wrapMode: Text.WordWrap
            font.pixelSize: Style.fontSizeExtraSmall
        }

        // Secondary, small, and only on request: the number is never the
        // answer, it is a footnote to the sentence above it.
        Label {
            x: Style.horizontalPageMargin
            width: parent.width - 2 * Style.horizontalPageMargin
            visible: Prefs.showNumbers && panel.hasNumber
            color: Style.secondaryColor
            font.pixelSize: Style.fontSizeExtraSmall
            wrapMode: Text.WordWrap
            text: {
                var parts = []
                if (panel.feedback && panel.feedback.wp !== undefined)
                    parts.push(qsTr("Gewinnaussicht %1 %").arg(Math.round(panel.feedback.wp * 100)))
                if (panel.feedback && panel.feedback.cp !== undefined)
                    parts.push(qsTr("Bewertung %1").arg((panel.feedback.cp / 100).toFixed(2)))
                return parts.join("  ·  ")
            }
        }

        Label {
            x: Style.horizontalPageMargin
            width: parent.width - 2 * Style.horizontalPageMargin
            visible: panel.showRuleKey && panel.feedback && panel.feedback.key !== undefined
            text: qsTr("Regel: %1").arg(panel.feedback && panel.feedback.key ? panel.feedback.key : "")
            color: Style.secondaryColor
            font.pixelSize: Style.fontSizeTiny
            wrapMode: Text.WordWrap
        }
    }
}
