pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import AD
import AD.Theme
import AD.Components

// A defended attack is on: fight it on the board, or let the engine play
// both sides at the campaign's difficulty.
Popup {
    id: prompt

    readonly property var o: GameController.battleOffer
    readonly property bool attacking: o.kind === "attack"

    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 540
    closePolicy: Popup.NoAutoClose
    // the offer stands until the battle is decided; once the board is up,
    // the prompt has been answered
    visible: GameController.battleOffered && !GameController.battle.active

    component Party: Column {
        property url flag
        property string name: ""
        property int units: 0
        property int power: 0
        property bool human: false
        spacing: 4
        width: 200
        Flag { anchors.horizontalCenter: parent.horizontalCenter; width: 64; height: 48; source: parent.flag }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: parent.name + (parent.human ? "  (you)" : "")
            font.family: Style.displayFamily
            font.pixelSize: Style.fontBody + 3
            font.bold: true
            color: Style.ink
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: parent.units + " units  ·  power " + parent.power
            font.pixelSize: Style.fontSmall + 1
            font.features: { "tnum": 1 }
            color: Style.inkFaint
        }
    }

    contentItem: Column {
        spacing: 14
        Shortcut { sequence: "A"; enabled: prompt.opened; onActivated: GameController.acceptBattle(true) }
        Shortcut { sequences: ["F", "Return", "Enter"]; enabled: prompt.opened; onActivated: GameController.acceptBattle(false) }
        Text {
            width: parent.width
            text: prompt.attacking ? "Assault on " + (prompt.o.toName || "")
                                   : (prompt.o.attackerName || "") + " attacks " + (prompt.o.toName || "")
            font.family: Style.displayFamily
            font.pixelSize: Style.fontTitle + 2
            font.bold: true
            color: Style.ink
            wrapMode: Text.Wrap
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 24
            Party {
                flag: prompt.o.attackerFlag || ""
                name: prompt.o.attackerName || ""
                units: prompt.o.attackers || 0
                power: prompt.o.attackerPower || 0
                human: prompt.o.attackerHuman === true
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "vs"
                font.family: Style.displayFamily
                font.pixelSize: Style.fontDisplay
                font.bold: true
                color: Style.brass
            }
            Party {
                flag: prompt.o.defenderFlag || ""
                name: prompt.o.defenderName || ""
                units: prompt.o.defenders || 0
                power: prompt.o.defenderPower || 0
                human: prompt.o.defenderHuman === true
            }
        }
        Text {
            visible: (prompt.o.sittingOut || 0) > 0
            width: parent.width
            text: prompt.o.sittingOut + " defenders beyond the board's cap sit this one out; they fall with the city if it does."
            font.pixelSize: Style.fontSmall
            color: Style.inkFaint
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            text: "Fight it yourself on the board, or let the engine play both sides at this campaign's difficulty."
            font.pixelSize: Style.fontBody
            color: Style.ink
            wrapMode: Text.Wrap
        }
        Row {
            anchors.right: parent.right
            spacing: 8
            Button {
                text: "Auto-resolve"
                onClicked: GameController.acceptBattle(true)
            }
            Button {
                id: fightButton
                text: "Fight on the board"
                primary: true
                focus: true
                onClicked: GameController.acceptBattle(false)
            }
        }
    }
    onOpened: fightButton.forceActiveFocus()
}
