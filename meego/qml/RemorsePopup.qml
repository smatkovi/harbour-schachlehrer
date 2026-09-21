import QtQuick 1.1
// Silica's RemorsePopup: an action is announced and runs a few seconds later
// unless it is tapped away. Kept because the pages rely on the delay, not
// merely on the looks.
Item {
    id: root
    property int delay: 4000
    property variant _action: null

    function execute(text, action) {
        root._action = action
        label.text = text
        root.visible = true
        timer.restart()
    }
    function cancel() {
        timer.stop()
        root.visible = false
        root._action = null
    }

    parent: null
    Component.onCompleted: {
        var page = root
        while (page && page.pageStack === undefined)
            page = page.parent
        if (page)
            root.parent = page
    }

    visible: false
    anchors { left: parent ? parent.left : undefined; right: parent ? parent.right : undefined
              bottom: parent ? parent.bottom : undefined }
    height: AppTheme.itemSizeLarge
    z: 200

    Rectangle { anchors.fill: parent; color: AppTheme.barColor }
    Text {
        id: label
        anchors {
            left: parent.left; leftMargin: AppTheme.horizontalPageMargin
            right: counter.left; rightMargin: AppTheme.paddingMedium
            verticalCenter: parent.verticalCenter
        }
        elide: Text.ElideRight
        color: AppTheme.primaryColor
        font.pixelSize: AppTheme.fontSizeSmall
    }
    Text {
        id: counter
        anchors { right: parent.right; rightMargin: AppTheme.horizontalPageMargin
                  verticalCenter: parent.verticalCenter }
        text: qsTr("Tap to undo")
        color: AppTheme.highlightColor
        font.pixelSize: AppTheme.fontSizeExtraSmall
    }
    MouseArea { anchors.fill: parent; onClicked: root.cancel() }

    Timer {
        id: timer
        interval: root.delay
        onTriggered: {
            root.visible = false
            if (root._action)
                root._action()
            root._action = null
        }
    }
}
