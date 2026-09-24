import QtQuick
import AD.Theme

// A page's name in the display face over a short brass rule, with an
// optional line beneath it.
Column {
    id: title

    property string text: ""
    property string subtitle: ""

    spacing: 6

    Text {
        text: title.text
        font.family: Style.displayFamily
        font.pixelSize: Style.fontDisplay
        font.bold: true
        font.letterSpacing: 3
        font.capitalization: Font.AllUppercase
        color: Style.onSlate
    }
    Rectangle {
        width: 56
        height: 2
        color: Style.brass
    }
    Text {
        visible: title.subtitle !== ""
        text: title.subtitle
        font.pixelSize: Style.fontBody
        color: Style.onSlateFaint
    }
}
