import QtQuick 1.1
// Silica's ListItem: the same tappable row as BackgroundItem here.
Item {
    id: root
    property bool highlighted: area.pressed
    property bool down: area.pressed
    // Silica's ListItem takes its height from contentHeight; the default is
    // the standard row height.
    property real contentHeight: AppTheme.itemSizeSmall
    signal clicked()
    width: parent ? parent.width : 0
    height: contentHeight
    Rectangle {
        anchors.fill: parent
        color: AppTheme.highlightColor
        opacity: area.pressed ? 0.25 : 0
    }
    MouseArea { id: area; anchors.fill: parent; onClicked: root.clicked() }
}
