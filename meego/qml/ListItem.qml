import QtQuick 1.1
// Silica's ListItem: the same tappable row as BackgroundItem here.
Item {
    id: root
    property bool highlighted: area.pressed
    property bool down: area.pressed
    signal clicked()
    width: parent ? parent.width : 0
    height: AppTheme.itemSizeSmall
    Rectangle {
        anchors.fill: parent
        color: AppTheme.highlightColor
        opacity: area.pressed ? 0.25 : 0
    }
    MouseArea { id: area; anchors.fill: parent; onClicked: root.clicked() }
}
