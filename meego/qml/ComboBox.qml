import QtQuick 1.1
import com.nokia.meego 1.0
// Silica's ComboBox: a label, the current value under it, and a menu of
// choices. Harmattan has nothing of the sort, so the choices are shown in a
// SelectionDialog, which is where a MeeGo user expects them.
//
// The surface is Silica's, so the pages did not have to change: label,
// description, currentIndex, value and a ContextMenu of MenuItems.
Item {
    id: root

    property string label: ""
    property string description: ""
    property int currentIndex: 0
    // Untyped on purpose: QtQuick 1.1 refuses an inline object on a
    // property declared with a component type.
    property variant menu: null
    property string value: menu ? menu.entryText(currentIndex) : ""
    signal currentIndexChangedByUser(int index)

    width: parent ? parent.width : 0
    height: column.height + 2 * AppTheme.paddingSmall

    Rectangle {
        anchors.fill: parent
        color: AppTheme.highlightColor
        opacity: area.pressed ? 0.25 : 0
    }

    Column {
        id: column
        anchors {
            left: parent.left; leftMargin: AppTheme.horizontalPageMargin
            right: parent.right; rightMargin: AppTheme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        spacing: 2

        Text {
            width: parent.width
            text: root.label
            color: AppTheme.secondaryColor
            font.pixelSize: AppTheme.fontSizeExtraSmall
            visible: text !== ""
        }
        Text {
            width: parent.width
            text: root.value
            color: AppTheme.primaryColor
            font.pixelSize: AppTheme.fontSizeSmall
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: root.description
            color: AppTheme.secondaryColor
            font.pixelSize: AppTheme.fontSizeExtraSmall
            wrapMode: Text.WordWrap
            visible: text !== ""
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        onClicked: {
            if (!root.menu)
                return
            var names = []
            for (var i = 0; i < root.menu.entryCount(); ++i)
                names.push(root.menu.entryText(i))
            dialog.model = names
            dialog.selectedIndex = root.currentIndex
            dialog.open()
        }
    }

    SelectionDialog {
        id: dialog
        titleText: root.label
        onAccepted: {
            root.currentIndex = dialog.selectedIndex
            root.menu.trigger(dialog.selectedIndex)
            root.currentIndexChangedByUser(dialog.selectedIndex)
        }
    }
}
