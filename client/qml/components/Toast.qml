import QtQuick
import AD.Theme

// One notice: a slate card with a coloured edge - lamp for good news,
// danger for bad, brass for the rest.
Rectangle {
    id: toast

    property string text: ""
    property string kind: "info"
    readonly property color accent: kind === "good" ? Style.lamp
                                  : kind === "bad" ? Style.danger : Style.brass

    width: 320
    implicitHeight: label.implicitHeight + 20
    radius: Style.radius
    color: Qt.rgba(Style.slateRaised.r, Style.slateRaised.g, Style.slateRaised.b, 0.96)
    border.width: 1
    border.color: Qt.alpha(toast.accent, 0.7)

    Rectangle {
        x: 1
        y: 1
        width: 4
        height: parent.height - 2
        radius: 2
        color: toast.accent
    }
    Text {
        id: label
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: toast.text
        font.pixelSize: Style.fontBody
        color: Style.onSlate
        wrapMode: Text.Wrap
    }
}
