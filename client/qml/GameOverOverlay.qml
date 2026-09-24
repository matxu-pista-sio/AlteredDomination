pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import AD
import AD.Theme
import AD.Components

// The end: victory or defeat over the dimmed chart, the final ranking, and
// the way back to the menu.
Rectangle {
    id: overlay

    signal menu()

    readonly property bool online: GameController.online && GameController.onlineOver
    readonly property var verdict: GameController.onlineOutcome
    readonly property bool victory: online ? verdict.won === true : GameController.victory
    readonly property bool draw: online && verdict.draw === true

    function reasonText() {
        switch (verdict.reason) {
        case "domination": return victory ? "You rule the world's economy" : (verdict.opponent || "The opponent") + " rules the world's economy"
        case "elimination": return victory ? (verdict.opponent || "The opponent") + " has no city left" : "Your last city has fallen"
        case "resign": return victory ? (verdict.opponent || "The opponent") + " resigned" : "You resigned"
        case "forfeit": return victory ? (verdict.opponent || "The opponent") + " never came back" : "You left the match"
        case "desync": return "The two campaigns diverged - the match is void"
        case "disputed": return "The two reports disagreed - the match is void"
        case "server_restart": return "The server restarted and lost the match"
        case "peer_disconnected": return "The opponent left before the campaign began"
        default: return verdict.reason || ""
        }
    }

    color: Qt.alpha(Style.ink, 0.72)
    visible: GameController.gameOver || online
    opacity: visible ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 500 } }

    MouseArea { anchors.fill: parent }  // swallow the map beneath

    Panel {
        anchors.centerIn: parent
        width: 520
        height: 520
        padding: 24

        Column {
            anchors.fill: parent
            spacing: 12
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: overlay.draw ? "NO VERDICT" : overlay.victory ? "VICTORY" : "DEFEAT"
                font.family: Style.displayFamily
                font.pixelSize: 54
                font.bold: true
                font.letterSpacing: 8
                color: overlay.draw ? Style.brass : overlay.victory ? Style.lamp : Style.danger
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: overlay.online
                      ? overlay.reasonText() + (overlay.verdict.disputed ? "  ·  no rating change"
                         : overlay.verdict.eloDelta !== undefined ? "  ·  " + (overlay.verdict.eloDelta >= 0 ? "+" : "") + overlay.verdict.eloDelta + " ELO" : "")
                      : overlay.victory
                      ? GameController.humanName + " rules " + GameController.sharePercent.toFixed(0) + " % of the world's income after " + GameController.round + " rounds"
                      : GameController.winnerName !== "" ? GameController.winnerName + " rules the world"
                                                         : GameController.humanName + " has fallen in round " + GameController.round
                font.pixelSize: Style.fontBody + 1
                color: Style.onSlate
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
            PipDivider { width: parent.width }
            SectionHeader { width: parent.width; text: "Final ranking" }
            ListView {
                id: list
                width: parent.width
                height: 260
                clip: true
                model: GameController.countries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    id: row
                    required property var model
                    width: list.width
                    height: 26
                    opacity: model.eliminated ? 0.4 : 1
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Text { width: 24; text: row.model.rank; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall; horizontalAlignment: Text.AlignRight }
                        Flag { width: 24; height: 18; source: row.model.flag }
                        Text { width: 200; text: row.model.name; color: row.model.me ? Style.brassBright : Style.onSlate; font.bold: row.model.me; font.pixelSize: Style.fontBody; elide: Text.ElideRight }
                        Text { text: row.model.share.toFixed(1) + " %  ·  " + row.model.cities + " cities"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall; font.features: { "tnum": 1 } }
                    }
                }
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Back to the menu"
                primary: true
                width: 220
                height: 40
                onClicked: overlay.menu()
            }
        }
    }
}
