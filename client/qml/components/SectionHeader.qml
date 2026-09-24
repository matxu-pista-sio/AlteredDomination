import QtQuick
import AD.Theme

// A section's name in small brass capitals, ruled to the right edge.
Row {
    id: header

    property string text: ""

    spacing: 10
    height: 22

    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: header.text
        font.family: Style.displayFamily
        font.pixelSize: Style.fontSmall + 1
        font.bold: true
        font.letterSpacing: 1.8
        font.capitalization: Font.AllUppercase
        color: Style.brassBright
    }
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, header.width - x)
        height: 1
        color: Style.brassDark
    }
}
