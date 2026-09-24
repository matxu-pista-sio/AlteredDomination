pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Layouts
import AD
import AD.Theme
import AD.Components

// The campaign's top bar: whose table this is, the round, the treasury,
// the world share against the domination line, and the End turn button -
// which the AI round's progress strip replaces while the world moves.
Panel {
    id: bar

    signal endTurn()
    signal menu()
    signal findCity()

    implicitHeight: 58
    padding: 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 14

        Flag {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 33
            source: GameController.humanFlag
        }
        Column {
            spacing: 1
            Text {
                text: GameController.humanName
                font.family: Style.displayFamily
                font.pixelSize: Style.fontBody + 4
                font.bold: true
                color: Style.onSlate
            }
            Text {
                text: "Round " + GameController.round + "  ·  rank " + GameController.rank
                      + "  ·  " + GameController.cityCount + (GameController.cityCount === 1 ? " city" : " cities")
                font.pixelSize: Style.fontSmall
                color: Style.onSlateFaint
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 34; color: Style.brassDark }

        Column {
            spacing: 0
            Text {
                text: "Treasury"
                font.pixelSize: Style.fontSmall - 1
                font.letterSpacing: 1.2
                font.capitalization: Font.AllUppercase
                color: Style.onSlateFaint
            }
            Row {
                spacing: 8
                Text {
                    text: GameController.formatNumber(GameController.funds)
                    font.family: Style.displayFamily
                    font.pixelSize: Style.fontTitle
                    font.bold: true
                    font.features: { "tnum": 1 }
                    color: Style.brassBright
                }
                Text {
                    anchors.baseline: parent.children[0].baseline
                    text: "+" + GameController.formatNumber(GameController.income) + " / round"
                    font.pixelSize: Style.fontSmall
                    font.features: { "tnum": 1 }
                    color: Style.onSlateFaint
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 34; color: Style.brassDark }

        Column {
            spacing: 3
            Text {
                text: GameController.sharePercent.toFixed(1) + " % of the world  ·  " + GameController.dominationPercent + " % wins"
                font.pixelSize: Style.fontSmall
                font.features: { "tnum": 1 }
                color: Style.onSlateFaint
            }
            Gauge {
                width: 200
                value: GameController.sharePercent
                threshold: GameController.dominationPercent
            }
        }

        Item { Layout.fillWidth: true }

        // the AI round
        Column {
            visible: GameController.aiThinking
            spacing: 3
            Text {
                anchors.right: parent.right
                text: GameController.aiStatus
                font.pixelSize: Style.fontSmall + 1
                color: Style.onSlate
            }
            Gauge {
                anchors.right: parent.right
                width: 220
                value: GameController.aiProgress * 100
                fillColor: Style.brass
            }
        }

        Button {
            visible: !GameController.aiThinking
            text: "End turn"
            primary: true
            enabled: GameController.humanTurn
            Layout.preferredWidth: 118
            Layout.preferredHeight: 36
            onClicked: bar.endTurn()
            ToolTip.visible: hovered
            ToolTip.text: "E"
        }
        Button {
            text: "Find"
            Layout.preferredWidth: 64
            onClicked: bar.findCity()
            ToolTip.visible: hovered
            ToolTip.text: "Find a city (F)"
        }
        Button {
            text: "Menu"
            Layout.preferredWidth: 70
            onClicked: bar.menu()
            ToolTip.visible: hovered
            ToolTip.text: "Escape"
        }
    }
}
