import QtQuick
import AD.Theme

// The paper grain (shaders/paper.frag): lay it over any flat surface and
// the surface becomes a sheet. Each instance rolls its own seed; bind
// `seed` when a surface should keep its sheet across rebuilds.
ShaderEffect {
    // The settings switch: off, the sheet is flat paint and the shader
    // does not run (an invisible ShaderEffect renders nothing).
    visible: Style.paperGrain
    property real seed: Math.random() * 32
    property real radius: 0
    property real strength: 1
    readonly property vector2d pxSize: Qt.vector2d(width, height)

    fragmentShader: "qrc:/shaders/paper.frag.qsb"
}
