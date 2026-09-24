import QtQuick
import QtQuick.Effects
import AD.Theme

// A Panel over the map that blurs what lies beneath it (the city sheet,
// the ranking), so the chart stays present under the glass. `backdrop`
// is the untransformed map viewport; with animations off the blur is
// skipped and the panel is a plain slate card.
Item {
    id: glass

    property Item backdrop: null
    property string title: ""
    property int padding: Style.spacing
    readonly property int headerHeight: title !== "" ? 30 : 0
    default property alias content: body.data

    ShaderEffectSource {
        id: source
        anchors.fill: parent
        visible: false
        live: true
        hideSource: false
        sourceItem: glass.backdrop
        sourceRect: glass.backdrop
                    ? glass.backdrop.mapFromItem(glass, 0, 0, glass.width, glass.height)
                    : Qt.rect(0, 0, 0, 0)
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: Style.radius
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        source: source
        visible: glass.backdrop !== null && Style.animations
        blurEnabled: true
        blur: 1.0
        blurMax: 40
        blurMultiplier: 0.6
        maskEnabled: true
        maskSource: mask
        autoPaddingEnabled: false
    }

    Rectangle {
        anchors.fill: parent
        radius: Style.radius
        color: Qt.rgba(Style.slateRaised.r, Style.slateRaised.g, Style.slateRaised.b,
                       glass.backdrop !== null && Style.animations ? 0.74 : 0.94)
        border.width: 1
        border.color: Style.brassDark

        Rectangle {
            visible: glass.title !== ""
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: glass.headerHeight
            radius: parent.radius - 1
            color: Qt.alpha(Style.slate, 0.55)

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Style.brassDark
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: glass.padding + 2
                anchors.verticalCenter: parent.verticalCenter
                text: glass.title
                font.family: Style.displayFamily
                font.pixelSize: Style.fontSmall + 1
                font.bold: true
                font.letterSpacing: 1.6
                font.capitalization: Font.AllUppercase
                color: Style.brassBright
            }
        }
    }

    Item {
        id: body
        anchors.fill: parent
        anchors.margins: glass.padding
        anchors.topMargin: glass.padding + glass.headerHeight
    }
}
