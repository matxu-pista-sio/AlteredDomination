import QtQuick
import AD.Theme

// A slate card with a brass hairline (docs/UI_THEME.md "Surfaces"): every
// HUD panel and popup is one. An optional title runs as a header strip.
Rectangle {
    id: panel

    property string title: ""
    property int padding: Style.spacing
    readonly property int headerHeight: title !== "" ? 30 : 0
    default property alias content: body.data

    radius: Style.radius
    color: Qt.rgba(Style.slateRaised.r, Style.slateRaised.g, Style.slateRaised.b, 0.94)
    border.width: 1
    border.color: Style.brassDark

    // A panel is solid: pointer events never fall through to the map beneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => { wheel.accepted = true }
    }

    Rectangle {
        id: header
        visible: panel.title !== ""
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 1
        height: panel.headerHeight
        radius: panel.radius - 1
        color: Qt.alpha(Style.slate, 0.7)

        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Style.brassDark
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: panel.padding + 2
            anchors.verticalCenter: parent.verticalCenter
            text: panel.title
            font.family: Style.displayFamily
            font.pixelSize: Style.fontSmall + 1
            font.bold: true
            font.letterSpacing: 1.6
            font.capitalization: Font.AllUppercase
            color: Style.brassBright
        }
    }

    Item {
        id: body
        anchors.fill: parent
        anchors.margins: panel.padding
        anchors.topMargin: panel.padding + panel.headerHeight
    }
}
