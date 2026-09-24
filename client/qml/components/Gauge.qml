import QtQuick
import AD.Theme

// A horizontal bar toward a threshold: the world share against the
// domination line, a force's power against the enemy's.
Item {
    id: gauge

    property real value: 0
    property real maximum: 100
    property real threshold: -1
    property color fillColor: Style.lamp
    property color trackColor: Style.slate

    implicitHeight: 8
    implicitWidth: 120

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: gauge.trackColor
        border.width: 1
        border.color: Style.brassDark
    }
    Rectangle {
        x: 1
        y: 1
        height: parent.height - 2
        width: Math.max(0, Math.min(1, gauge.value / gauge.maximum)) * (parent.width - 2)
        radius: height / 2
        color: gauge.fillColor
        Behavior on width {
            enabled: Style.animations
            NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
        }
    }
    Rectangle {
        visible: gauge.threshold >= 0
        x: Math.min(1, gauge.threshold / gauge.maximum) * parent.width - 1
        y: -2
        width: 2
        height: parent.height + 4
        color: Style.brassBright
    }
}
