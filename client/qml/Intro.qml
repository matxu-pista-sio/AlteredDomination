import QtQuick
import QtQuick.Shapes
import AD.Theme

// The reveal: a pool of lamplight opens on the table, the mark rises out
// of the dark and the name sets itself letter by letter. Any key or click
// skips it.
FocusScope {
    id: intro

    signal done()

    focus: true
    Keys.onPressed: (event) => { event.accepted = true; intro.finish() }
    TapHandler { onTapped: intro.finish() }

    function finish() {
        if (intro.finished) return
        intro.finished = true
        intro.done()
    }
    property bool finished: false

    Rectangle { anchors.fill: parent; color: Style.slate }

    // the pool of light
    Shape {
        id: pool
        anchors.centerIn: parent
        width: parent.width * 1.1
        height: parent.height * 1.1
        preferredRendererType: Shape.CurveRenderer
        opacity: 0
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: pool.width / 2; centerY: pool.height / 2
                focalX: centerX; focalY: centerY
                centerRadius: pool.width / 2
                GradientStop { position: 0.0; color: Qt.alpha(Style.lamp, 0.22) }
                GradientStop { position: 0.35; color: Qt.alpha(Style.lamp, 0.06) }
                GradientStop { position: 1.0; color: "transparent" }
            }
            PathAngleArc {
                centerX: pool.width / 2; centerY: pool.height / 2
                radiusX: pool.width / 2; radiusY: pool.height / 2
                startAngle: 0; sweepAngle: 360
            }
        }
    }

    Image {
        id: mark
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.22
        width: Math.min(parent.width * 0.32, 420)
        fillMode: Image.PreserveAspectFit
        source: "qrc:/assets/branding/adlogo.svg"
        sourceSize: Qt.size(840, 560)
        opacity: 0
        scale: 1.12
    }

    Text {
        id: name
        anchors.horizontalCenter: parent.horizontalCenter
        y: mark.y + mark.paintedHeight + 28
        text: "ALTERED DOMINATION"
        font.family: Style.displayFamily
        font.pixelSize: 44
        font.bold: true
        font.letterSpacing: 18
        color: Style.paper
        opacity: 0
    }

    Rectangle {
        id: rule
        anchors.horizontalCenter: parent.horizontalCenter
        y: name.y + name.height + 14
        width: 0
        height: 2
        color: Style.brass
    }

    Text {
        id: tagline
        anchors.horizontalCenter: parent.horizontalCenter
        y: rule.y + 18
        text: "the world under one banner"
        font.pixelSize: Style.fontBody + 2
        font.letterSpacing: 3
        color: Style.onSlateFaint
        opacity: 0
    }

    // the brass sweep across the mark
    Rectangle {
        id: sweep
        width: 3
        height: parent.height
        x: -10
        color: Style.brassBright
        opacity: 0
    }

    SequentialAnimation {
        running: true
        ParallelAnimation {
            NumberAnimation { target: pool; property: "opacity"; to: 1; duration: 900; easing.type: Easing.InOutQuad }
            NumberAnimation { target: mark; property: "opacity"; to: 1; duration: 900; easing.type: Easing.OutCubic }
            NumberAnimation { target: mark; property: "scale"; to: 1; duration: 1100; easing.type: Easing.OutCubic }
            SequentialAnimation {
                PauseAnimation { duration: 300 }
                NumberAnimation { target: sweep; property: "opacity"; to: 0.55; duration: 120 }
                NumberAnimation { target: sweep; property: "x"; from: intro.width * 0.3; to: intro.width * 0.7; duration: 520; easing.type: Easing.InOutQuad }
                NumberAnimation { target: sweep; property: "opacity"; to: 0; duration: 160 }
            }
        }
        ParallelAnimation {
            NumberAnimation { target: name; property: "opacity"; to: 1; duration: 600 }
            NumberAnimation { target: name; property: "font.letterSpacing"; from: 18; to: 6; duration: 900; easing.type: Easing.OutCubic }
            NumberAnimation { target: rule; property: "width"; to: 240; duration: 700; easing.type: Easing.OutCubic }
        }
        NumberAnimation { target: tagline; property: "opacity"; to: 1; duration: 500 }
        PauseAnimation { duration: 700 }
        ScriptAction { script: intro.finish() }
    }
}
