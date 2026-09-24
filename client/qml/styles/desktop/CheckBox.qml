import QtQuick
import QtQuick.Templates as T
import AD.Theme

// A brass-rimmed square; checked fills it with brass and an ink tick.
T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    spacing: 8
    font.pixelSize: Style.fontBody

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 3
        color: control.checked ? (control.down ? Style.brassDark : Style.brass) : Style.slate
        border.width: 1
        border.color: control.visualFocus ? Style.lamp
                    : control.checked || control.hovered ? Style.brassBright : Style.brassDark
        Behavior on color { ColorAnimation { duration: 80 } }

        Text {
            anchors.centerIn: parent
            text: "✓"
            font.pixelSize: 14
            font.bold: true
            color: Style.ink
            visible: control.checked
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Style.onSlate : Style.onSlateFaint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
