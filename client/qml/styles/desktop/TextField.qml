import QtQuick
import QtQuick.Templates as T
import AD.Theme

// A clear writing surface: white paper ruled with a brass border that
// wakes on focus.
T.TextField {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    padding: 4
    leftPadding: 10
    rightPadding: 10

    font.pixelSize: Style.fontBody
    color: control.enabled ? Style.ink : Style.inkFaint
    selectionColor: Style.brass
    selectedTextColor: Style.ink
    placeholderTextColor: Style.inkFaint
    verticalAlignment: TextInput.AlignVCenter

    Text {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height - control.topPadding - control.bottomPadding
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        elide: Text.ElideRight
        visible: !control.length && !control.preeditText
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: Style.controlHeight
        radius: Style.radius
        color: control.enabled ? Style.white : Style.paperDark
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Style.brass
                    : control.hovered ? Style.brass : Style.brassDark
        opacity: control.enabled ? 1.0 : 0.7
    }
}
