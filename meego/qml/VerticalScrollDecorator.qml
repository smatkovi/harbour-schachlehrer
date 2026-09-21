import QtQuick 1.1
// Silica's VerticalScrollDecorator: the thin bar that shows where you are in
// a Flickable. It is declared inside the Flickable, so the parent is it.
Rectangle {
    id: bar
    property Flickable flickable: parent

    width: 4
    radius: 2
    color: AppTheme.highlightColor
    anchors.right: parent ? parent.right : undefined
    visible: flickable && flickable.contentHeight > flickable.height

    height: flickable && flickable.contentHeight > 0
            ? Math.max(30, flickable.height * (flickable.height / flickable.contentHeight))
            : 0
    y: flickable && flickable.contentHeight > flickable.height
       ? (flickable.contentY / (flickable.contentHeight - flickable.height))
         * (flickable.height - height)
       : 0

    opacity: flickable && flickable.moving ? 0.8 : 0
    Behavior on opacity { NumberAnimation { duration: 300 } }
}
