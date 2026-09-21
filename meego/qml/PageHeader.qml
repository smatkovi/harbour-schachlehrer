import QtQuick 1.1
// Silica's PageHeader: the page title, right-aligned, with room above it.
Item {
    property string title: ""
    property alias description: sub.text
    width: parent ? parent.width : 0
    height: AppTheme.itemSizeLarge + (sub.text !== "" ? sub.height : 0)
    Column {
        anchors {
            right: parent.right; rightMargin: AppTheme.horizontalPageMargin
            left: parent.left; leftMargin: AppTheme.horizontalPageMargin
            bottom: parent.bottom; bottomMargin: AppTheme.paddingMedium
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            text: title
            color: AppTheme.highlightColor
            font.pixelSize: AppTheme.fontSizeLarge
        }
        Text {
            id: sub
            width: parent.width
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.WordWrap
            visible: text !== ""
            color: AppTheme.secondaryColor
            font.pixelSize: AppTheme.fontSizeExtraSmall
        }
    }
}
