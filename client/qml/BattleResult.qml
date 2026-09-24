pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import AD
import AD.Theme
import AD.Components

// The battle's end over the dimmed board: won, lost or drawn, the losses
// on both sides by type, and the way back to the chart.
Rectangle {
    id: overlay

    signal leave()

    readonly property var bc: GameController.battle
    readonly property var result: bc.result
    readonly property string key: bc.resultKey

    color: Qt.alpha(Style.ink, 0.7)
    visible: bc.over
    opacity: visible ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 400 } }

    MouseArea { anchors.fill: parent }

    component Losses: Column {
        property var list: []
        property string heading: ""
        spacing: 4
        width: 200
        Text {
            text: parent.heading
            font.family: Style.displayFamily
            font.pixelSize: Style.fontBody + 2
            font.bold: true
            color: Style.brassBright
        }
        Repeater {
            model: parent.list
            Row {
                id: loss
                required property var modelData
                spacing: 8
                Rectangle {
                    width: 22; height: 22; radius: 11
                    color: Style.brass
                    Image { anchors.fill: parent; anchors.margins: 3; source: loss.modelData.icon; sourceSize: Qt.size(44, 44) }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: loss.modelData.count + " × " + loss.modelData.name
                    font.pixelSize: Style.fontBody
                    color: Style.onSlate
                }
            }
        }
        Text {
            visible: parent.list.length === 0
            text: "No losses"
            font.pixelSize: Style.fontBody
            color: Style.onSlateFaint
        }
    }

    Panel {
        anchors.centerIn: parent
        width: 560
        height: 420
        padding: 24

        Column {
            anchors.fill: parent
            spacing: 14
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: overlay.key === "won" ? "VICTORY" : overlay.key === "lost" ? "DEFEAT" : "STALEMATE"
                font.family: Style.displayFamily
                font.pixelSize: 48
                font.bold: true
                font.letterSpacing: 8
                color: overlay.key === "won" ? Style.lamp : overlay.key === "lost" ? Style.danger : Style.brassBright
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    const w = overlay.result.winner
                    const city = overlay.bc.cityName
                    if (w === "attacker") return overlay.bc.attacker.name + " takes " + city + " after " + overlay.result.turns + " turns"
                    if (w === "defender") return overlay.bc.defender.name + " holds " + city + " after " + overlay.result.turns + " turns"
                    return "Both sides keep their survivors after " + overlay.result.turns + " turns"
                }
                font.pixelSize: Style.fontBody + 1
                color: Style.onSlate
            }
            PipDivider { width: parent.width }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 40
                Losses { heading: overlay.bc.attacker.name + " lost"; list: overlay.result.attackerLosses || [] }
                Losses { heading: overlay.bc.defender.name + " lost"; list: overlay.result.defenderLosses || [] }
            }
            Item { width: 1; height: 4 }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Back to the chart"
                primary: true
                width: 220
                height: 40
                onClicked: overlay.leave()
            }
        }
    }
}
