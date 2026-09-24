import QtQuick
import QtQuick.Templates as T
import AD.Theme

// Paper-coloured on the slate chrome by default; a label that sits on a
// paper surface (popups, cards) sets color: Style.ink.
T.Label {
    color: Style.onSlate
    linkColor: Style.brassBright
    font.pixelSize: Style.fontBody
}
