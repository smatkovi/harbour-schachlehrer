import QtQuick 1.1
import com.nokia.meego 1.0
// Silica's TextSwitch: a label with a switch, the whole row tappable.
Item {
    id: root
    property string text: ""
    property string description: ""
    property bool checked: false
    property bool automaticCheck: true
    signal clicked()

    width: parent ? parent.width : 0
    height: Math.max(toggle.height, column.height) + 2 * AppTheme.paddingSmall

    Column {
        id: column
        anchors {
            left: parent.left; leftMargin: AppTheme.horizontalPageMargin
            right: toggle.left; rightMargin: AppTheme.paddingMedium
            verticalCenter: parent.verticalCenter
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.text
            color: root.checked ? AppTheme.primaryColor : AppTheme.secondaryColor
            font.pixelSize: AppTheme.fontSizeSmall
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.description !== ""
            text: root.description
            color: AppTheme.secondaryColor
            font.pixelSize: AppTheme.fontSizeExtraSmall
        }
    }

    Switch {
        id: toggle
        anchors {
            right: parent.right; rightMargin: AppTheme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        checked: root.checked
        onCheckedChanged: if (checked !== root.checked) root.checked = checked
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            if (root.automaticCheck)
                root.checked = !root.checked
            root.clicked()
        }
    }
}
