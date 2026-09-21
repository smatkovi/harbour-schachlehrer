import QtQuick 1.1
// Silica's VerticalScrollDecorator: the thin bar showing where you are in a
// Flickable. It is declared inside the Flickable, so that is the parent --
// but typing the property as Flickable makes QtQuick 1.1 refuse the
// assignment ("Unable to assign QObject* to QDeclarativeFlickable*"), so it
// is read untyped and guarded.
Rectangle {
    id: bar
    property variant view: parent

    width: 4
    radius: 2
    color: AppTheme.highlightColor
    anchors.right: parent ? parent.right : undefined

    // Guarded one by one: at the moment these first evaluate, parent is
    // not yet the Flickable and the properties read as undefined.
    property real viewHeight: (view && view.height !== undefined) ? view.height : 0
    property real viewContentHeight:
        (view && view.contentHeight !== undefined) ? view.contentHeight : 0
    property real viewContentY:
        (view && view.contentY !== undefined) ? view.contentY : 0
    property bool scrollable: viewContentHeight > viewHeight && viewHeight > 0

    visible: scrollable
    height: scrollable ? Math.max(30, viewHeight * (viewHeight / viewContentHeight)) : 0
    y: scrollable
       ? (viewContentY / (viewContentHeight - viewHeight)) * (viewHeight - height)
       : 0

    opacity: (view && view.moving) ? 0.8 : 0
    Behavior on opacity { NumberAnimation { duration: 300 } }
}
