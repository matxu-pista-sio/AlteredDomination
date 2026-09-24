import QtQuick
import QtQuick.Controls
import AD.Theme

// A unit type as a card: its icon on a banner disc, name, cost and
// classes. Recruit tiles, codex entries and force pickers are all this.
Rectangle {
    id: tile

    property string typeKey: ""
    property string name: ""
    property int cost: 0
    property url icon
    property string classText: ""
    property string description: ""
    property color banner: Style.brass
    property bool selected: false
    property bool affordable: true
    property int count: -1
    property bool compact: false

    signal clicked()

    implicitWidth: compact ? 200 : 128
    implicitHeight: compact ? 52 : 128
    radius: Style.radius
    color: selected ? Qt.alpha(Style.brass, 0.22)
         : hover.hovered ? Style.slateLight : Style.slate
    border.width: 1
    border.color: selected ? Style.brassBright : hover.hovered ? Style.brass : Style.brassDark
    opacity: affordable ? 1.0 : 0.5

    Behavior on color { ColorAnimation { duration: 90 } }

    Rectangle {
        id: disc
        x: tile.compact ? 8 : (parent.width - width) / 2
        y: tile.compact ? (parent.height - height) / 2 : 12
        width: tile.compact ? 36 : 56
        height: width
        radius: width / 2
        color: tile.banner
        border.width: 1
        border.color: Qt.alpha(Style.paper, 0.35)

        Image {
            anchors.fill: parent
            anchors.margins: tile.compact ? 4 : 6
            source: tile.icon
            sourceSize: Qt.size(64, 64)
            fillMode: Image.PreserveAspectFit
            smooth: true
        }
    }

    Text {
        id: nameLabel
        anchors.left: tile.compact ? disc.right : parent.left
        anchors.right: parent.right
        anchors.leftMargin: tile.compact ? 10 : 6
        anchors.rightMargin: 6
        y: tile.compact ? 9 : disc.y + disc.height + 8
        text: tile.name
        font.family: Style.displayFamily
        font.pixelSize: Style.fontBody + 1
        font.bold: true
        horizontalAlignment: tile.compact ? Text.AlignLeft : Text.AlignHCenter
        elide: Text.ElideRight
        color: Style.onSlate
    }
    Text {
        anchors.left: nameLabel.left
        anchors.right: parent.right
        anchors.rightMargin: 6
        y: nameLabel.y + nameLabel.height + 1
        text: tile.count >= 0 ? "× " + tile.count + "  ·  " + tile.cost : tile.cost + "  ·  " + tile.classText
        font.pixelSize: Style.fontSmall
        horizontalAlignment: tile.compact ? Text.AlignLeft : Text.AlignHCenter
        elide: Text.ElideRight
        color: tile.affordable ? Style.brassBright : Style.danger
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: tile.clicked() }

    ToolTip.visible: hover.hovered && tile.description !== ""
    ToolTip.text: tile.description
    ToolTip.delay: 600
}
