import QtQuick 1.1
import com.nokia.meego 1.0
// Silica's PullDownMenu. Harmattan has no pull-down gesture, so the same
// entries are reached through a button at the top right of the page, which is
// where a MeeGo user looks for a menu anyway. The page's own tools bar was not
// used: it lives at the bottom and would cover the board.
Item {
    id: root
    default property alias entries: layout.children

    // The menu is anchored to the page, not to wherever it was declared.
    parent: null
    Component.onCompleted: {
        var page = root
        while (page && page.pageStack === undefined)
            page = page.parent
        if (page)
            root.parent = page
    }

    anchors { top: parent ? parent.top : undefined; right: parent ? parent.right : undefined }
    width: AppTheme.itemSizeSmall
    height: AppTheme.itemSizeSmall
    z: 100

    Rectangle {
        anchors.fill: parent
        color: AppTheme.highlightColor
        opacity: area.pressed ? 0.3 : 0
        radius: 4
    }
    Text {
        anchors.centerIn: parent
        text: "⋮"
        color: AppTheme.highlightColor
        font.pixelSize: AppTheme.fontSizeLarge
    }
    MouseArea {
        id: area
        anchors.fill: parent
        onClicked: menu.open()
    }

    Menu {
        id: menu
        MenuLayout { id: layout }
    }
}
