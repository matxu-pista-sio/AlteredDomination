import QtQuick
import AD.Theme

// A unit on the board: its icon on a banner-coloured disc, a brass crown
// ring for a general, dimmed once it has acted; it slides between cells
// and, when destroyed, falls after the projectile has landed.
Item {
    id: chip

    property url icon
    property color banner: Style.brass
    property bool general: false
    property bool acted: false
    property bool alive: true
    property bool selected: false
    property bool mine: false
    property int fallDelay: 320

    readonly property real disc: Math.min(width, height) * 0.86

    Behavior on x { enabled: Style.animations; NumberAnimation { duration: 220; easing.type: Easing.InOutQuad } }
    Behavior on y { enabled: Style.animations; NumberAnimation { duration: 220; easing.type: Easing.InOutQuad } }

    Rectangle {
        id: body
        anchors.centerIn: parent
        width: chip.disc
        height: chip.disc
        radius: width / 2
        color: chip.banner
        border.width: chip.selected ? 2.5 : chip.general ? 2 : 1
        border.color: chip.selected ? Style.select : chip.general ? Style.brassBright : Qt.alpha(Style.paper, 0.45)
        opacity: chip.acted ? 0.55 : 1

        Rectangle {
            visible: chip.general
            anchors.centerIn: parent
            width: parent.width + 8
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 1.5
            border.color: Style.brassBright
            opacity: 0.8
        }
        Image {
            anchors.fill: parent
            anchors.margins: chip.disc * 0.14
            source: chip.icon
            sourceSize: Qt.size(96, 96)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
        // the crown
        Text {
            visible: chip.general
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: -4
            text: "♛"
            font.pixelSize: chip.disc * 0.42
            color: Style.brassBright
            style: Text.Outline
            styleColor: Style.ink
        }
    }

    onAliveChanged: if (!alive) fall.start()

    SequentialAnimation {
        id: fall
        PauseAnimation { duration: chip.fallDelay }
        ParallelAnimation {
            NumberAnimation { target: body; property: "scale"; to: 0.15; duration: 380; easing.type: Easing.InBack }
            NumberAnimation { target: body; property: "opacity"; to: 0; duration: 380 }
            NumberAnimation { target: body; property: "rotation"; to: 70; duration: 380 }
        }
        PropertyAction { target: chip; property: "visible"; value: false }
    }
}
