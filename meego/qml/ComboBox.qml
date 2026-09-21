import QtQuick 1.1
import com.nokia.meego 1.0

// QtQuick.Controls' ComboBox, done the Harmattan way: a button showing the
// current entry, opening a SelectionDialog. Only the surface the settings
// and learning pages use -- model, currentIndex and activated(index) -- so
// none of those call sites had to change beyond dropping the arrow function,
// which QtQuick 1.1's ES5 engine cannot parse.
Item {
    id: root

    property variant model: []
    property int currentIndex: 0
    property string title: ""
    signal activated(int index)

    implicitHeight: button.height
    height: implicitHeight

    function entryAt(index) {
        if (!model || index < 0 || index >= model.length)
            return ""
        return model[index]
    }

    Button {
        id: button
        anchors.fill: parent
        text: root.entryAt(root.currentIndex)
        onClicked: dialog.open()
    }

    SelectionDialog {
        id: dialog
        titleText: root.title
        selectedIndex: root.currentIndex
        // A ListView model may be a plain JS array in QtQuick 1.1, which is
        // what the pages pass.
        model: root.model
        onAccepted: {
            root.currentIndex = dialog.selectedIndex
            root.activated(dialog.selectedIndex)
        }
    }
}
