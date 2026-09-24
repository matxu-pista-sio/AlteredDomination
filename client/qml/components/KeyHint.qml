import QtQuick
import AD.Theme

// A key cap: the shortcut a control answers to, drawn as a small slate
// key with a brass rim.
Rectangle {
    id: cap

    property string key: ""

    implicitWidth: Math.max(22, label.implicitWidth + 12)
    implicitHeight: 20
    radius: 4
    color: Style.slate
    border.width: 1
    border.color: Style.brassDark

    Text {
        id: label
        anchors.centerIn: parent
        text: cap.key
        font.family: Style.bodyFamily
        font.pixelSize: Style.fontSmall
        font.bold: true
        color: Style.brassBright
    }
}
