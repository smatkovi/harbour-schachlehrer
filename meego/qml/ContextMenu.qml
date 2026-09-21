import QtQuick 1.1
// Silica's ContextMenu, as ComboBox uses it: a holder for the MenuItems that
// make up the choices. It draws nothing itself -- ComboBox reads the entries
// and puts them in a SelectionDialog, which is how Harmattan asks.
Item {
    id: root
    default property alias entries: root.children
    visible: false

    function entryCount() { return root.children.length }
    function entryText(index) {
        var child = root.children[index]
        return child && child.text !== undefined ? child.text : ""
    }
    function trigger(index) {
        var child = root.children[index]
        if (child && child.clicked)
            child.clicked()
    }
}
