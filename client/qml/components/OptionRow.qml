import QtQuick
import AD.Theme

// A settings line: the option's name (and a hint under it) on the left,
// its control on the right.
Item {
    id: row

    property string label: ""
    property string hint: ""
    default property alias control: slot.data

    implicitHeight: Math.max(40, slot.childrenRect.height + 8, labels.implicitHeight + 8)

    Column {
        id: labels
        anchors.left: parent.left
        anchors.right: slot.left
        anchors.rightMargin: Style.spacing
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            text: row.label
            font.pixelSize: Style.fontBody
            color: Style.onSlate
        }
        Text {
            visible: row.hint !== ""
            text: row.hint
            font.pixelSize: Style.fontSmall
            color: Style.onSlateFaint
            wrapMode: Text.Wrap
            width: labels.width
        }
    }
    Item {
        id: slot
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        height: childrenRect.height
    }
}
