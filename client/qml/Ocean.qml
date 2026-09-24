import QtQuick
import AD.Theme

// The water under the chart (shaders/ocean.frag): slow layered swell, a
// faint graticule, darker toward the poles. Fills the viewport; the camera
// tells it which map units it covers.
ShaderEffect {
    id: ocean

    property vector2d mapSize: Qt.vector2d(4096, 2019)
    property vector2d mapOrigin: Qt.vector2d(0, 0)
    property color deep: Style.oceanDeep
    property color shallow: Style.ocean
    property real time: 0
    property real waves: Style.oceanWaves ? 1.0 : 0.0
    property real graticule: 0.55
    property real zoom: 1

    fragmentShader: "qrc:/shaders/ocean.frag.qsb"

    NumberAnimation on time {
        running: Style.oceanWaves && Style.animations
        loops: Animation.Infinite
        from: 0
        to: 36000
        duration: 36000000
    }
}
