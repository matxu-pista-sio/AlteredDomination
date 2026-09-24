import QtQuick
import QtQuick.Templates as T
import AD.Theme

// The table's button (docs/UI_THEME.md): a raised slate block with a brass
// fitting. `highlighted`/`checked` is the LIT state - brass fill, ink text -
// so an engaged order reads at arm's length. `primary` fills with the lamp
// (the one action a page wants), `danger` with the lamp turned red.
T.Button {
    id: control

    property bool primary: false
    property bool danger: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 4
    horizontalPadding: 14

    font.family: Style.displayFamily
    font.pixelSize: Style.fontBody + 1
    font.bold: true
    font.letterSpacing: 0.6

    readonly property bool lit: highlighted || checked
    readonly property bool filled: lit || primary || danger

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: !control.enabled
               ? (control.filled ? Style.slateLight : Style.onSlateFaint)
               : control.filled ? Style.ink : Style.onSlate
    }

    background: Rectangle {
        implicitWidth: 72
        implicitHeight: Style.controlHeight
        radius: Style.radius
        color: {
            if (!control.enabled)
                return control.filled ? Style.brassDark : Style.slateRaised
            if (control.danger)
                return control.down ? Qt.darker(Style.danger, 1.3)
                     : control.hovered ? Qt.lighter(Style.danger, 1.12) : Style.danger
            if (control.primary)
                return control.down ? Style.lampDark
                     : control.hovered ? Qt.lighter(Style.lamp, 1.1) : Style.lamp
            if (control.lit)
                return control.down ? Style.brassDark
                     : control.hovered ? Style.brassBright : Style.brass
            if (control.down)
                return Style.slate
            if (control.hovered)
                return Style.slateLight
            return Style.slateRaised
        }
        border.width: 1
        border.color: {
            if (!control.enabled) return Style.slateLight
            if (control.visualFocus) return Style.lamp
            if (control.filled) return Style.brassBright
            if (control.hovered) return Style.brass
            return Style.brassDark
        }
        opacity: control.enabled ? 1.0 : 0.55

        Behavior on color { ColorAnimation { duration: 80 } }
        Behavior on border.color { ColorAnimation { duration: 80 } }

        // One light line along the top edge sells the machined relief
        // without a gradient; it dims while pressed, so the block reads
        // as sunk.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: 1
            radius: parent.radius - 1
            color: Qt.rgba(1.0, 0.95, 0.8,
                           control.down ? 0.04 : control.filled ? 0.45 : 0.14)
        }
    }
}
