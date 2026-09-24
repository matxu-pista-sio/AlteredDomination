pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import AD
import AD.Theme
import AD.Components

// Which units go: the source city's ready units by type, every count
// defaulting to all of them, the force's power against the target's.
Popup {
    id: picker

    signal failed(string text)
    signal done(string result)

    property int mode: GameController.Moving
    property int fromId: -1
    property var target: ({})
    property var groups: []
    property var counts: []
    readonly property bool attacking: mode === GameController.Attacking
    readonly property int chosenPower: {
        let p = 0
        for (let i = 0; i < groups.length; ++i) p += groups[i].cost * (counts[i] || 0)
        return p
    }
    readonly property int chosenCount: {
        let n = 0
        for (let i = 0; i < counts.length; ++i) n += counts[i] || 0
        return n
    }

    function openFor(mode, from, target) {
        picker.mode = mode
        picker.fromId = from
        picker.target = target
        if (GameController.selectedCity !== from) GameController.selectedCity = from
        groups = GameController.cityUnits.groups()
        const c = []
        for (const g of groups) c.push(g.unacted)
        counts = c
        open()
    }
    function setCount(i, n) {
        const c = counts.slice()
        c[i] = n
        counts = c
    }
    function setAll(all) {
        const c = []
        for (const g of groups) c.push(all ? g.unacted : 0)
        counts = c
    }
    function confirm() {
        const ids = []
        for (let i = 0; i < groups.length; ++i)
            for (let k = 0; k < (counts[i] || 0); ++k) ids.push(groups[i].unactedIds[k])
        if (ids.length === 0) return
        const r = attacking ? GameController.attack(fromId, target.id, ids)
                            : GameController.moveUnits(fromId, target.id, ids)
        if (r === "ok" || r === "captured" || r === "needs-battle" || r === "resolved") {
            Audio.play("click")
            GameController.interaction = GameController.Browse
            close()
            done(r)
        } else {
            failed((attacking ? "Attack refused: " : "Move refused: ") + r)
        }
    }

    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
    closePolicy: Popup.CloseOnEscape
    onOpened: confirmButton.forceActiveFocus()

    contentItem: Column {
        spacing: 12

        // Shortcuts live in the item tree, whatever holds the focus.
        Shortcut {
            sequences: ["Return", "Enter"]
            enabled: picker.opened
            onActivated: picker.confirm()
        }

        Text {
            text: (picker.attacking ? "Attack " : "Move to ") + (picker.target.name || "")
            font.family: Style.displayFamily
            font.pixelSize: Style.fontTitle
            font.bold: true
            color: Style.ink
        }
        Row {
            visible: picker.attacking
            spacing: 10
            Flag { width: 36; height: 27; source: picker.target.flag || "" }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: "Held by " + (picker.target.ownerName || "") + (picker.target.capital ? "  ·  their capital" : "")
                    font.pixelSize: Style.fontBody
                    color: Style.ink
                }
                Text {
                    text: picker.target.undefended
                          ? "Undefended - captured on arrival"
                          : (picker.target.unitCount || 0) + " defenders, power " + (picker.target.power || 0) + " - a board battle"
                    font.pixelSize: Style.fontSmall
                    color: picker.target.undefended ? Style.lampDark : Style.danger
                }
            }
        }
        Text {
            visible: !picker.attacking
            text: "Your city  ·  " + (picker.target.unitCount || 0) + " units there, power " + (picker.target.power || 0)
            font.pixelSize: Style.fontSmall
            color: Style.inkFaint
        }

        Rectangle { width: parent.width; height: 1; color: Style.brassDark }

        Repeater {
            model: picker.groups
            Row {
                id: line
                required property var modelData
                required property int index
                spacing: 10
                height: 34
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: 28; radius: 14
                    color: Style.brass
                    Image { anchors.fill: parent; anchors.margins: 4; source: line.modelData.icon; sourceSize: Qt.size(48, 48) }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 170
                    text: line.modelData.name
                    font.pixelSize: Style.fontBody
                    color: Style.ink
                    elide: Text.ElideRight
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 70
                    text: line.modelData.unacted + " ready"
                    font.pixelSize: Style.fontSmall
                    color: Style.inkFaint
                }
                SpinBox {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 120
                    from: 0
                    to: line.modelData.unacted
                    value: picker.counts[line.index] || 0
                    onValueModified: picker.setCount(line.index, value)
                }
            }
        }

        Row {
            spacing: 8
            Button { text: "All"; onClicked: picker.setAll(true) }
            Button { text: "None"; onClicked: picker.setAll(false) }
        }

        Rectangle { width: parent.width; height: 1; color: Style.brassDark }

        Row {
            width: parent.width
            spacing: 12
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 220
                Text {
                    text: picker.chosenCount + " units, power " + picker.chosenPower
                          + (picker.attacking && !picker.target.undefended ? "  vs  " + picker.target.power : "")
                    font.pixelSize: Style.fontBody
                    font.bold: true
                    color: Style.ink
                }
                Gauge {
                    visible: picker.attacking && !picker.target.undefended
                    width: parent.width
                    value: picker.chosenPower
                    maximum: Math.max(1, picker.chosenPower + (picker.target.power || 0))
                    threshold: Math.max(1, picker.chosenPower + (picker.target.power || 0)) / 2
                    fillColor: Style.lamp
                    trackColor: Style.danger
                }
            }
            Button { text: "Cancel"; onClicked: picker.close() }
            Button {
                id: confirmButton
                text: picker.attacking ? "Attack" : "Move"
                primary: !picker.attacking
                danger: picker.attacking
                enabled: picker.chosenCount > 0
                onClicked: picker.confirm()
            }
        }
    }
}
