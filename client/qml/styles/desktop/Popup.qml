import QtQuick
import QtQuick.Templates as T
import AD.Theme

// The default popup surface: a paper sheet with a brass rim, over a
// dimmed table.
T.Popup {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: Style.spacing * 2

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
    }

    background: Rectangle {
        radius: Style.radius
        color: Style.paper
        border.width: 1
        border.color: Style.brassDark

        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 4
            anchors.leftMargin: 2
            anchors.rightMargin: -2
            radius: parent.radius
            color: Qt.alpha(Style.ink, 0.5)
        }
    }

    T.Overlay.modal: Rectangle { color: Qt.alpha(Style.ink, 0.55) }
    T.Overlay.modeless: Rectangle { color: Qt.alpha(Style.ink, 0.25) }
}
