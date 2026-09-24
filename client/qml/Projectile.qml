import QtQuick
import QtQuick.Particles
import AD.Theme

// A strike in flight: a brass tracer along the line, then a burst of
// sparks where it lands. fire() plays one shot; `landed` fires on impact.
Item {
    id: shot

    signal landed()

    property real fromX: 0
    property real fromY: 0
    property real toX: 0
    property real toY: 0
    property int flightMs: 300

    function fire(x1, y1, x2, y2) {
        fromX = x1; fromY = y1; toX = x2; toY = y2
        tracer.x = x1 - tracer.width / 2
        tracer.y = y1 - tracer.height / 2
        tracer.visible = true
        flight.restart()
    }

    Rectangle {
        id: tracer
        width: 10
        height: 10
        radius: 5
        color: Style.brassBright
        border.width: 1
        border.color: Style.paper
        visible: false
        Rectangle {
            anchors.centerIn: parent
            width: 22
            height: 22
            radius: 11
            color: Qt.alpha(Style.lamp, 0.35)
        }
    }

    ParticleSystem { id: sparks }
    ImageParticle {
        system: sparks
        source: "qrc:/assets/units/icons/soldier.svg"
        visible: false
    }
    ItemParticle {
        system: sparks
        delegate: Rectangle {
            width: 5 + Math.random() * 4
            height: width
            radius: width / 2
            color: Math.random() < 0.5 ? Style.lamp : Style.brassBright
        }
    }
    Emitter {
        id: burst
        system: sparks
        enabled: false
        lifeSpan: 550
        lifeSpanVariation: 200
        velocity: AngleDirection { angleVariation: 180; magnitude: 140; magnitudeVariation: 90 }
        acceleration: PointDirection { y: 220 }
        size: 6
        endSize: 1
        sizeVariation: 3
    }

    SequentialAnimation {
        id: flight
        ParallelAnimation {
            NumberAnimation { target: tracer; property: "x"; to: shot.toX - tracer.width / 2; duration: shot.flightMs; easing.type: Easing.InQuad }
            NumberAnimation { target: tracer; property: "y"; to: shot.toY - tracer.height / 2; duration: shot.flightMs; easing.type: Easing.InQuad }
        }
        ScriptAction {
            script: {
                tracer.visible = false
                burst.x = shot.toX
                burst.y = shot.toY
                burst.burst(38)
                shot.landed()
            }
        }
    }
}
