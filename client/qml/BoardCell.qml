import QtQuick
import AD.Theme

// One cell of the tactical grid: a slate checker, tinted at either edge
// for the deploy zones, lit for the selection's legal moves and strikes.
Rectangle {
    id: cell

    property bool dark: false
    property int zone: -1        // 0 attacker's, 1 defender's, -1 open ground
    property color zoneColor: Style.brass
    property int highlight: 0    // BattleController.Highlight
    property bool hovered: false
    property bool cursor: false

    color: dark ? Style.boardDark : Style.boardLight
    border.width: 1
    border.color: Qt.alpha(Style.ink, 0.5)

    Rectangle {
        visible: cell.zone >= 0
        anchors.fill: parent
        color: Qt.alpha(cell.zoneColor, 0.13)
    }
    Rectangle {
        visible: cell.highlight > 0
        anchors.fill: parent
        anchors.margins: 2
        radius: 3
        color: cell.highlight === 1 ? Qt.alpha(Style.moveTarget, 0.35)
             : cell.highlight === 2 ? Qt.alpha(Style.attackTarget, 0.4)
             : Qt.alpha(Style.select, 0.15)
        border.width: 2
        border.color: cell.highlight === 1 ? Style.moveTarget
                    : cell.highlight === 2 ? Style.attackTarget : Style.select
    }
    Rectangle {
        visible: cell.hovered || cell.cursor
        anchors.fill: parent
        color: "transparent"
        border.width: cell.cursor ? 2 : 1
        border.color: cell.cursor ? Style.lamp : Qt.alpha(Style.paper, 0.6)
    }
}
