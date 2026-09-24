import QtQuick
import QtQuick.Templates as T
import AD.Theme

// A slim brass rail that fades out when the content is still.
T.ScrollBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 2
    visible: control.policy !== T.ScrollBar.AlwaysOff
    minimumSize: 0.1

    contentItem: Rectangle {
        implicitWidth: control.interactive ? 8 : 4
        implicitHeight: control.interactive ? 8 : 4
        radius: width / 2
        color: control.pressed ? Style.brassBright : Style.brass
        opacity: control.policy === T.ScrollBar.AlwaysOn
                 || (control.active && control.size < 1.0) ? 0.85 : 0.0

        Behavior on opacity { NumberAnimation { duration: 250 } }
    }
}
