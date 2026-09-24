import QtQuick
import QtQuick.Templates as T
import AD.Theme

// A toggle: a slate pill whose knob slides to a lit brass end.
T.Switch {
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
        implicitWidth: 40
        implicitHeight: 20
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 10
        color: control.checked ? Style.brass : Style.slate
        border.width: 1
        border.color: control.visualFocus ? Style.lamp : control.checked ? Style.brassBright : Style.brassDark
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            x: Math.max(2, Math.min(parent.width - width - 2,
                                    control.visualPosition * parent.width - width / 2))
            y: 2
            width: 16
            height: 16
            radius: 8
            color: control.checked ? Style.ink : Style.paper
            border.width: 1
            border.color: control.checked ? Style.brassDark : Style.brassDark
            Behavior on x {
                enabled: !control.down
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? Style.onSlate : Style.onSlateFaint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
