pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import AD.Theme

// A writing field between two brass steppers.
T.SpinBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding
                            + up.implicitIndicatorWidth + down.implicitIndicatorWidth)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding,
                             implicitBackgroundHeight,
                             up.implicitIndicatorHeight, down.implicitIndicatorHeight)

    padding: 4
    leftPadding: padding + (control.mirrored ? up.indicator.width : down.indicator.width)
    rightPadding: padding + (control.mirrored ? down.indicator.width : up.indicator.width)

    font.pixelSize: Style.fontBody
    editable: true

    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: Style.ink
        selectionColor: Style.brass
        selectedTextColor: Style.ink
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        clip: width < implicitWidth
    }

    component Stepper: Rectangle {
        required property bool pressed
        required property bool hovered
        required property string glyph
        implicitWidth: Style.controlHeight
        implicitHeight: Style.controlHeight
        color: pressed ? Style.brassDark : hovered ? Style.brassBright : Style.brass
        Text {
            anchors.centerIn: parent
            text: parent.glyph
            font.pixelSize: 16
            font.bold: true
            color: Style.ink
        }
    }

    up.indicator: Stepper {
        x: control.mirrored ? 0 : control.width - width
        height: control.height
        pressed: control.up.pressed
        hovered: control.up.hovered
        glyph: "+"
        radius: Style.radius
        // square the inner corners
        Rectangle { width: Style.radius; height: parent.height; color: parent.color; x: control.mirrored ? parent.width - width : 0 }
    }

    down.indicator: Stepper {
        x: control.mirrored ? control.width - width : 0
        height: control.height
        pressed: control.down.pressed
        hovered: control.down.hovered
        glyph: "−"
        radius: Style.radius
        Rectangle { width: Style.radius; height: parent.height; color: parent.color; x: control.mirrored ? 0 : parent.width - width }
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: Style.controlHeight
        radius: Style.radius
        color: control.enabled ? Style.white : Style.paperDark
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Style.brass : Style.brassDark
    }
}
