import QtQuick
import QtQuick.Shapes
import AD.Theme

// A city on the chart: a ring sized by tier in the owner's colour, a brass
// star for a capital, the garrison's power under it and the name above,
// with the selection pulse and the move/attack target rings. Authored in
// pixels at the fit zoom; the map scales it by the square root of the
// zoom so it never bloats.
Item {
    id: marker

    property string name: ""
    property int tier: 0
    property bool capital: false
    property color ownerColor: Style.brass
    property int unitCount: 0
    property int power: 0
    property int highlight: 0        // WorldModel.Highlight
    property bool selected: false
    property bool mine: false
    property bool hovered: false
    property bool showLabel: true
    property bool showPower: false

    readonly property real ring: 3.2 + tier * 1.3
    readonly property color targetColor: highlight === 1 ? Style.moveTarget
                                       : highlight === 2 ? Style.attackTarget : Style.select

    width: 40
    height: 40

    // Hit-testing follows the ring, not the 40 px box the label needs: an
    // invisible circle answers contains() for the marker.
    containmentMask: hitShape
    Shape {
        id: hitShape
        width: marker.width
        height: marker.height
        visible: false
        containsMode: Shape.FillContains
        ShapePath {
            strokeWidth: -1
            PathAngleArc {
                centerX: marker.width / 2; centerY: marker.height / 2
                radiusX: marker.ring + 4; radiusY: marker.ring + 4
                startAngle: 0; sweepAngle: 360
            }
        }
    }

    // the target / selection ring
    Rectangle {
        visible: marker.selected || marker.highlight > 0
        anchors.centerIn: parent
        width: marker.ring * 2 + 9
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 1.6
        border.color: marker.targetColor

        SequentialAnimation on scale {
            running: (marker.selected || marker.highlight > 0) && Style.animations
            loops: Animation.Infinite
            NumberAnimation { to: 1.25; duration: 700; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutSine }
        }
    }
    Rectangle {
        visible: marker.selected && Style.animations
        anchors.centerIn: parent
        width: marker.ring * 2 + 9
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 1.2
        border.color: Style.select
        SequentialAnimation on scale {
            running: marker.selected && Style.animations
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 2.2; duration: 1400; easing.type: Easing.OutQuad }
        }
        SequentialAnimation on opacity {
            running: marker.selected && Style.animations
            loops: Animation.Infinite
            NumberAnimation { from: 0.8; to: 0.0; duration: 1400 }
        }
    }

    // the ring
    Rectangle {
        anchors.centerIn: parent
        width: marker.ring * 2
        height: width
        radius: width / 2
        color: marker.ownerColor
        border.width: marker.hovered ? 1.6 : 1.0
        border.color: marker.mine ? Style.paper : Qt.alpha(Style.ink, 0.85)
        Behavior on color { enabled: Style.animations; ColorAnimation { duration: 600 } }

        Rectangle {
            visible: marker.unitCount > 0
            anchors.centerIn: parent
            width: Math.max(2, parent.width * 0.36)
            height: width
            radius: width / 2
            color: Qt.alpha(Style.ink, 0.6)
        }
    }

    // a capital's star
    Shape {
        visible: marker.capital
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 0
        width: 12
        height: 12
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: Style.brassBright
            strokeColor: Qt.alpha(Style.ink, 0.8)
            strokeWidth: 0.6
            startX: 6; startY: 0
            PathLine { x: 7.6; y: 4.2 }
            PathLine { x: 12; y: 4.4 }
            PathLine { x: 8.5; y: 7.2 }
            PathLine { x: 9.7; y: 11.5 }
            PathLine { x: 6; y: 9 }
            PathLine { x: 2.3; y: 11.5 }
            PathLine { x: 3.5; y: 7.2 }
            PathLine { x: 0; y: 4.4 }
            PathLine { x: 4.4; y: 4.2 }
            PathLine { x: 6; y: 0 }
        }
        scale: (marker.ring * 2 + 2) / 12
    }

    // the garrison's power
    Rectangle {
        visible: marker.showPower && marker.power > 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: marker.height / 2 + marker.ring + 2
        width: powerLabel.implicitWidth + 6
        height: 10
        radius: 3
        color: Qt.alpha(Style.ink, 0.78)
        border.width: 0.5
        border.color: marker.mine ? Style.brass : Qt.alpha(Style.paper, 0.4)
        Text {
            id: powerLabel
            anchors.centerIn: parent
            text: marker.power
            font.pixelSize: 7
            font.bold: true
            color: marker.mine ? Style.brassBright : Style.paper
        }
    }

    // the name
    Text {
        visible: marker.showLabel || marker.hovered || marker.selected
        anchors.horizontalCenter: parent.horizontalCenter
        y: marker.height / 2 - marker.ring - 3 - height
        text: marker.name
        font.family: Style.bodyFamily
        font.pixelSize: marker.tier >= 2 ? 9.5 : 8.5
        font.bold: marker.tier >= 2 || marker.capital
        font.letterSpacing: 0.2
        color: marker.hovered || marker.selected ? Style.select : Style.paper
        style: Text.Outline
        styleColor: Qt.alpha(Style.ink, 0.9)
    }
}
