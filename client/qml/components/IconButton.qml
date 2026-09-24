import QtQuick
import QtQuick.Controls
import AD.Theme

// A square button holding one glyph, with the tooltip every icon button
// owes the player.
Button {
    id: button

    property string glyph: ""
    property string tip: ""

    text: glyph
    implicitWidth: Style.controlHeight + 2
    horizontalPadding: 0
    font.family: Style.bodyFamily
    font.pixelSize: Style.fontBody + 2
    font.letterSpacing: 0
    ToolTip.visible: hovered && tip !== ""
    ToolTip.text: tip
}
