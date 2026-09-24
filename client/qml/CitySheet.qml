pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import AD
import AD.Theme
import AD.Components

// The selected city over the map's glass: who holds it, what it yields,
// the garrison by type, and - when it is yours on your turn - Recruit,
// Move and Attack. A foreign city lists your cities that can strike it.
GlassPanel {
    id: sheet

    signal flyTo(int id)
    signal openForce(int mode, int from, var target)
    signal failed(string text)

    readonly property int cityId: GameController.selectedCity
    property var info: ({})
    property bool recruiting: false
    property int recruitIndex: -1
    property int recruitQty: 1
    readonly property var recruitType: recruitIndex >= 0 ? GameController.catalog.info(recruitKeys[recruitIndex]) : null
    readonly property var recruitKeys: {
        const keys = []
        for (let i = 0; i < GameController.catalog.count; ++i)
            keys.push(GameController.catalog.data(GameController.catalog.index(i, 0), Qt.UserRole + 1))
        return keys
    }
    readonly property bool mine: info.mine === true
    readonly property bool canAct: mine && GameController.humanTurn

    function refresh() {
        info = cityId >= 0 ? GameController.cityInfo(cityId) : {}
    }
    onCityIdChanged: { recruiting = false; refresh() }
    Connections {
        target: GameController
        function onStateChanged() { sheet.refresh() }
    }

    function recruit() {
        if (!recruitType) return
        const r = GameController.recruit(cityId, recruitType.typeKey, recruitQty)
        if (r === "") Audio.play("click")
        else sheet.failed("Recruit refused: " + r)
    }

    width: 340
    padding: 12

    Column {
        anchors.fill: parent
        spacing: 8

        // -- header ------------------------------------------------------------
        Item {
            width: parent.width
            height: 52
            Column {
                anchors.left: parent.left
                anchors.right: closeButton.left
                spacing: 1
                Text {
                    width: parent.width
                    text: sheet.info.name || ""
                    font.family: Style.displayFamily
                    font.pixelSize: Style.fontTitle + 2
                    font.bold: true
                    color: Style.onSlate
                    elide: Text.ElideRight
                }
                Text {
                    text: (sheet.info.capital ? "Capital of " + sheet.info.countryName + "  ·  " : "")
                          + "tier " + ((sheet.info.tier || 0) + 1) + "  ·  " + (sheet.info.links || 0) + " links"
                    font.pixelSize: Style.fontSmall
                    color: Style.onSlateFaint
                }
            }
            IconButton {
                id: closeButton
                anchors.right: parent.right
                anchors.top: parent.top
                glyph: "✕"
                tip: "Close (Escape)"
                onClicked: GameController.selectedCity = -1
            }
        }

        Row {
            spacing: 10
            Flag { width: 40; height: 30; source: sheet.info.flag || "" }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: (sheet.mine ? "Yours" : "Held by " + (sheet.info.ownerName || ""))
                    font.pixelSize: Style.fontBody
                    font.bold: true
                    color: sheet.mine ? Style.brassBright : Style.onSlate
                }
                Text {
                    text: "Income " + (sheet.info.income || 0) + "  ·  power " + (sheet.info.power || 0)
                          + "  ·  " + (sheet.info.unitCount || 0) + " units"
                    font.pixelSize: Style.fontSmall
                    font.features: { "tnum": 1 }
                    color: Style.onSlateFaint
                }
            }
        }

        PipDivider { width: parent.width }

        // -- the garrison ------------------------------------------------------
        SectionHeader { width: parent.width; text: sheet.recruiting ? "Recruit" : "Garrison" }

        // the garrison list
        ListView {
            id: garrison
            visible: !sheet.recruiting
            width: parent.width
            height: Math.min(contentHeight, 200)
            clip: true
            spacing: 3
            model: GameController.cityUnits
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: row
                required property var model
                width: garrison.width
                height: 30
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Rectangle {
                        width: 24; height: 24; radius: 12
                        color: sheet.info.ownerColor || Style.brass
                        Image { anchors.fill: parent; anchors.margins: 3; source: row.model.icon; sourceSize: Qt.size(48, 48) }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 150
                        text: row.model.name
                        font.pixelSize: Style.fontBody
                        color: Style.onSlate
                        elide: Text.ElideRight
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "× " + row.model.count
                              + (sheet.mine && row.model.unacted < row.model.count ? "  (" + row.model.unacted + " ready)" : "")
                        font.pixelSize: Style.fontSmall
                        font.features: { "tnum": 1 }
                        color: Style.brassBright
                    }
                }
            }
        }
        Text {
            visible: !sheet.recruiting && GameController.cityUnits.total === 0
            text: "No garrison - an attack captures it outright"
            font.pixelSize: Style.fontSmall
            color: Style.danger
        }

        // the recruit panel
        Column {
            visible: sheet.recruiting
            width: parent.width
            spacing: 6
            Grid {
                columns: 4
                spacing: 4
                Repeater {
                    model: GameController.catalog
                    Rectangle {
                        id: tile
                        required property var model
                        required property int index
                        readonly property bool affordable: model.cost <= GameController.funds
                        width: (sheet.width - 24 - 12) / 4
                        height: 58
                        radius: Style.radius
                        color: sheet.recruitIndex === index ? Qt.alpha(Style.brass, 0.25) : tileHover.hovered ? Style.slateLight : Style.slate
                        border.width: 1
                        border.color: sheet.recruitIndex === index ? Style.brassBright : Style.brassDark
                        opacity: affordable ? 1 : 0.45
                        HoverHandler { id: tileHover }
                        TapHandler { onTapped: { sheet.recruitIndex = tile.index; sheet.recruitQty = 1 } }
                        Column {
                            anchors.centerIn: parent
                            spacing: 2
                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 26; height: 26
                                source: tile.model.icon
                                sourceSize: Qt.size(52, 52)
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: tile.model.cost
                                font.pixelSize: Style.fontSmall
                                font.bold: true
                                color: tile.affordable ? Style.brassBright : Style.danger
                            }
                        }
                        ToolTip.visible: tileHover.hovered
                        ToolTip.text: tile.model.name + " · " + tile.model.cost + " · " + tile.model.classText + "\n" + tile.model.description
                        ToolTip.delay: 400
                    }
                }
            }
            Row {
                visible: sheet.recruitType !== null
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 120
                    text: sheet.recruitType ? sheet.recruitType.name : ""
                    font.pixelSize: Style.fontBody
                    font.bold: true
                    color: Style.onSlate
                    elide: Text.ElideRight
                }
                SpinBox {
                    id: qty
                    from: 1
                    to: Math.max(1, Math.floor(GameController.funds / (sheet.recruitType ? sheet.recruitType.cost : 1)))
                    value: sheet.recruitQty
                    onValueModified: sheet.recruitQty = value
                    width: 110
                }
                Button {
                    text: "Max"
                    width: 50
                    onClicked: sheet.recruitQty = qty.to
                }
            }
            Row {
                visible: sheet.recruitType !== null
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Total " + (sheet.recruitType ? sheet.recruitType.cost * sheet.recruitQty : 0)
                          + " of " + GameController.formatNumber(GameController.funds)
                    font.pixelSize: Style.fontSmall
                    font.features: { "tnum": 1 }
                    color: Style.onSlateFaint
                }
                Button {
                    text: "Recruit"
                    primary: true
                    enabled: sheet.recruitType !== null && sheet.recruitType.cost * sheet.recruitQty <= GameController.funds
                    onClicked: sheet.recruit()
                }
            }
        }

        // -- actions -----------------------------------------------------------
        Item { width: 1; height: 4 }
        Row {
            visible: sheet.canAct
            spacing: 6
            Button {
                text: "Recruit"
                checkable: true
                checked: sheet.recruiting
                onClicked: { sheet.recruiting = !sheet.recruiting; GameController.interaction = GameController.Browse }
                ToolTip.visible: hovered
                ToolTip.text: "R"
            }
            Button {
                text: "Move"
                checkable: true
                checked: GameController.interaction === GameController.Moving
                enabled: GameController.cityUnits.unacted > 0
                onClicked: { sheet.recruiting = false; GameController.interaction = checked ? GameController.Moving : GameController.Browse }
                ToolTip.visible: hovered
                ToolTip.text: "M"
            }
            Button {
                text: "Attack"
                checkable: true
                checked: GameController.interaction === GameController.Attacking
                enabled: GameController.cityUnits.unacted > 0
                onClicked: { sheet.recruiting = false; GameController.interaction = checked ? GameController.Attacking : GameController.Browse }
                ToolTip.visible: hovered
                ToolTip.text: "A"
            }
        }
        Text {
            visible: sheet.canAct && GameController.interaction !== GameController.Browse
            width: parent.width
            text: GameController.interaction === GameController.Moving
                  ? "Click a green city to move there. Escape cancels."
                  : "Click a red city to attack it. Escape cancels."
            font.pixelSize: Style.fontSmall
            color: GameController.interaction === GameController.Moving ? Style.moveTarget : Style.attackTarget
            wrapMode: Text.Wrap
        }
        Text {
            visible: sheet.mine && !GameController.humanTurn
            text: GameController.aiThinking ? "The world is moving…" : "Not your turn"
            font.pixelSize: Style.fontSmall
            color: Style.onSlateFaint
        }

        // a foreign city: where an attack can come from
        Column {
            visible: !sheet.mine && GameController.humanTurn && GameController.active
            width: parent.width
            spacing: 4
            SectionHeader { width: parent.width; text: "Attack from" }
            Repeater {
                model: GameController.attackSources(sheet.cityId)
                Button {
                    required property var modelData
                    width: parent.width
                    text: modelData.name + "  ·  " + modelData.unacted + " ready, power " + modelData.power
                    danger: true
                    onClicked: {
                        const targets = GameController.attackTargets(modelData.id)
                        for (const t of targets)
                            if (t.id === sheet.cityId) { sheet.openForce(GameController.Attacking, modelData.id, t); return }
                    }
                }
            }
            Text {
                visible: GameController.attackSources(sheet.cityId).length === 0
                text: "None of your cities with ready units is linked to it"
                font.pixelSize: Style.fontSmall
                color: Style.onSlateFaint
                wrapMode: Text.Wrap
                width: parent.width
            }
        }

        Button {
            text: "Fly to"
            onClicked: sheet.flyTo(sheet.cityId)
        }
    }
}
