import QtQuick 1.1
// Silica's SectionHeader: a right-aligned heading with a rule under it.
Item {
    property string text: ""
    width: parent ? parent.width : 0
    height: label.height + AppTheme.paddingLarge
    Text {
        id: label
        anchors {
            right: parent.right; rightMargin: AppTheme.horizontalPageMargin
            left: parent.left; leftMargin: AppTheme.horizontalPageMargin
            bottom: parent.bottom; bottomMargin: AppTheme.paddingSmall
        }
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
        text: parent.text
        color: AppTheme.highlightColor
        font.pixelSize: AppTheme.fontSizeSmall
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.leftMargin: AppTheme.horizontalPageMargin
        anchors.rightMargin: AppTheme.horizontalPageMargin
        height: 1
        color: AppTheme.secondaryColor
        opacity: 0.4
    }
}
