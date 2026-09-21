import QtQuick 1.1
// Silica's IconButton. Silica icon sources are not on this device, so the
// caller's icon.source is shown if it resolves and a text fallback otherwise.
Item {
    id: root
    property alias icon: image
    property string text: ""
    property bool enabled: true
    signal clicked()
    width: AppTheme.itemSizeSmall
    height: AppTheme.itemSizeSmall
    opacity: enabled ? 1 : 0.4
    Image {
        id: image
        anchors.centerIn: parent
        fillMode: Image.PreserveAspectFit
        width: parent.width * 0.6
        height: width
        visible: status === Image.Ready
    }
    Text {
        anchors.centerIn: parent
        visible: image.status !== Image.Ready
        text: root.text
        color: AppTheme.highlightColor
        font.pixelSize: AppTheme.fontSizeSmall
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
