import QtQuick 1.1
// com.nokia.meego is imported under a name so that the Slider below is the
// platform's and not this file itself.
import com.nokia.meego 1.0 as Meego

// Silica's Slider: a caption above the bar and the current value beside it.
// The MeeGo Slider has neither, so they are drawn here and the bar itself is
// the platform's.
Item {
    id: root

    property real minimumValue: 0
    property real maximumValue: 100
    property real value: 0
    property real stepSize: 1
    property string label: ""
    property string valueText: ""
    property bool pressed: bar.pressed

    width: parent ? parent.width : 0
    height: caption.height + bar.height + 2 * AppTheme.paddingSmall

    Text {
        id: caption
        anchors {
            left: parent.left; leftMargin: AppTheme.horizontalPageMargin
            right: valueLabel.left; rightMargin: AppTheme.paddingMedium
            top: parent.top
        }
        text: root.label
        color: AppTheme.secondaryColor
        font.pixelSize: AppTheme.fontSizeExtraSmall
        elide: Text.ElideRight
    }
    Text {
        id: valueLabel
        anchors {
            right: parent.right; rightMargin: AppTheme.horizontalPageMargin
            baseline: caption.baseline
        }
        text: root.valueText
        color: AppTheme.highlightColor
        font.pixelSize: AppTheme.fontSizeExtraSmall
    }

    Meego.Slider {
        id: bar
        anchors {
            left: parent.left; leftMargin: AppTheme.horizontalPageMargin
            right: parent.right; rightMargin: AppTheme.horizontalPageMargin
            top: caption.bottom
        }
        minimumValue: root.minimumValue
        maximumValue: root.maximumValue
        stepSize: root.stepSize
        value: root.value
        onValueChanged: if (root.value !== value) root.value = value
    }
}
