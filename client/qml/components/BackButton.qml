import QtQuick
import QtQuick.Controls

// The way back: every page has one top-left, and Escape presses it.
Button {
    text: "◂  Back"
    ToolTip.visible: hovered
    ToolTip.text: "Escape"
}
