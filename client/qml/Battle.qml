pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Layouts
import AD
import AD.Theme
import AD.Components

// The board battle (docs/GAME_DESIGN.md §8, docs/UI_THEME.md "The battle
// screen"): the tactical grid projected on the table, standing upright -
// the player's side at the bottom, the enemy at the top, the enemy's edge
// upward like the codex diagrams. The info panel stands on the left, the
// orders on the right. Every action goes through GameController.battle.
FocusScope {
    id: page

    signal finished()

    focus: true

    readonly property var bc: GameController.battle
    /// Along the board (the attacker's edge to the defender's): screen rows.
    readonly property int length: page.bc.boardLength
    /// Across the board: screen columns.
    readonly property int across: page.bc.boardWidth
    /// The defender views the board turned around: its edge at the bottom.
    readonly property bool mirrored: page.bc.viewSide === 1
    readonly property int mySide: page.bc.viewSide
    readonly property int enemySide: 1 - page.bc.viewSide
    readonly property var me: mirrored ? page.bc.defender : page.bc.attacker
    readonly property var enemy: mirrored ? page.bc.attacker : page.bc.defender
    readonly property int cell: Math.max(24, Math.floor(Math.min((boardArea.width - 24) / across,
                                                                (boardArea.height - 48) / length)))
    readonly property var highlightMap: {
        const m = {}
        for (const h of page.bc.highlights) m[h.x + "," + h.y] = h.kind
        return m
    }
    property int cursorCol: -1
    property int cursorRow: -1
    property int hoverX: -1
    property int hoverY: -1
    property var pendingStrike: null

    // board (x along, y across) <-> screen (row, column)
    function screenRow(x) { return page.mirrored ? x : page.length - 1 - x }
    function screenCol(y) { return page.mirrored ? page.across - 1 - y : y }
    function boardX(row) { return page.mirrored ? row : page.length - 1 - row }
    function boardY(col) { return page.mirrored ? page.across - 1 - col : col }
    function centerOf(x, y) { return Qt.point(screenCol(y) * cell + cell / 2, screenRow(x) * cell + cell / 2) }
    function sideColor(side) { return side === 0 ? page.bc.attacker.color : page.bc.defender.color }
    function moveCursor(dc, dr) {
        if (cursorCol < 0) { cursorCol = Math.floor(across / 2); cursorRow = length - 2; return }
        cursorCol = Math.max(0, Math.min(across - 1, cursorCol + dc))
        cursorRow = Math.max(0, Math.min(length - 1, cursorRow + dr))
    }
    function showCard(x, y) {
        const u = page.bc.unitAt(x, y)
        if (!u.typeKey) return
        unitCard.info = GameController.catalog.info(u.typeKey)
        unitCard.state = (u.side === 0 ? page.bc.attacker.name : page.bc.defender.name)
                         + (u.general ? "  ·  general" : "") + (u.acted ? "  ·  has acted this turn" : "")
        unitCard.x = Math.min(page.width - unitCard.width - 12,
                              boardArea.x + board.x + screenCol(y) * cell + cell + 8)
        unitCard.y = Math.max(12, Math.min(page.height - unitCard.height - 12,
                                           boardArea.y + board.y + screenRow(x) * cell - 20))
        unitCard.open()
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Escape:
            if (unitCard.opened) unitCard.close()
            else if (page.bc.selectedX >= 0) page.bc.clearSelection()
            else if (page.bc.over) page.bc.leave()
            else quitDialog.open()
            break
        case Qt.Key_Return: case Qt.Key_Enter:
            if (page.bc.over) page.bc.leave()
            else if (page.bc.canReady) page.bc.ready()
            else if (page.bc.myTurn) page.bc.endTurn()
            break
        case Qt.Key_D: if (page.bc.myTurn) page.bc.offerDraw(); break
        case Qt.Key_Left: moveCursor(-1, 0); break
        case Qt.Key_Right: moveCursor(1, 0); break
        case Qt.Key_Up: moveCursor(0, -1); break
        case Qt.Key_Down: moveCursor(0, 1); break
        case Qt.Key_Space: if (cursorCol >= 0) page.bc.cellClicked(boardX(cursorRow), boardY(cursorCol)); break
        default: return
        }
        event.accepted = true
    }

    Component.onCompleted: page.forceActiveFocus()
    StackView.onActivated: page.forceActiveFocus()

    Rectangle { anchors.fill: parent; color: Style.slate }

    // the lamp's pool on the table
    Rectangle {
        x: layout.x + boardArea.x + (boardArea.width - width) / 2
        y: layout.y + boardArea.y + (boardArea.height - height) / 2
        width: boardArea.width * 1.3
        height: boardArea.height * 1.15
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.alpha(Style.lamp, 0.07) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    Connections {
        target: page.bc
        function onActionPerformed(a) {
            if (a.kind === "strike") {
                const from = page.centerOf(a.fromX, a.fromY)
                const to = page.centerOf(a.toX, a.toY)
                page.pendingStrike = a
                shot.fire(from.x, from.y, to.x, to.y)
                Audio.playUnit(a.typeKey, "fire")
            } else if (a.kind === "move" || a.kind === "swap") {
                Audio.play("click")
            } else if (a.kind === "promote") {
                Audio.play("hoverhome")
            }
        }
        function onFinished() { page.finished() }
        function onNotice(text) { GameController.notice(text, "info") }
    }
    Connections {
        target: GameController
        // online: the server's verdict ends the match wherever the board stands
        function onOnlineChanged() { if (GameController.onlineOver) page.finished() }
    }

    component Party: Column {
        id: party
        property var info: ({})
        property int side: 0
        property bool mine: false
        spacing: 3
        Row {
            spacing: 10
            Flag { width: 44; height: 33; source: party.info.flag || "" }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: (party.info.name || "") + (party.mine ? "  (you)" : "")
                    font.family: Style.displayFamily
                    font.pixelSize: Style.fontBody + 4
                    font.bold: true
                    color: party.mine ? Style.brassBright : Style.onSlate
                }
                Text {
                    readonly property int units: party.side === 0 ? page.bc.attackerUnits : page.bc.defenderUnits
                    readonly property int generals: party.side === 0 ? page.bc.attackerGenerals : page.bc.defenderGenerals
                    readonly property int power: party.side === 0 ? page.bc.attackerPower : page.bc.defenderPower
                    text: units + " units  ·  power " + power + "  ·  " + generals + " general" + (generals === 1 ? "" : "s")
                    font.pixelSize: Style.fontSmall
                    font.features: { "tnum": 1 }
                    color: Style.onSlateFaint
                }
            }
        }
        Rectangle {
            width: 220
            height: 3
            radius: 1.5
            color: party.info.color || Style.brass
        }
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // -- the situation ---------------------------------------------------------
        Panel {
            Layout.preferredWidth: 270
            Layout.fillHeight: true
            padding: 16

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Text {
                    text: page.bc.phase === 0 ? "DEPLOY" : page.bc.phase === 1 ? "PROMOTE"
                        : page.bc.phase === 2 ? "TURN " + page.bc.turn : "BATTLE OVER"
                    font.family: Style.displayFamily
                    font.pixelSize: Style.fontTitle + 4
                    font.bold: true
                    font.letterSpacing: 3
                    color: Style.brassBright
                }
                Row {
                    spacing: 8
                    Item {
                        width: 16; height: 16
                        visible: page.bc.busy
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle { anchors.fill: parent; radius: 8; color: "transparent"; border.width: 2; border.color: Style.lamp; opacity: 0.35 }
                        Rectangle { width: 6; height: 6; radius: 3; x: 5; y: -1; color: Style.lamp }
                        RotationAnimation on rotation {
                            running: page.bc.busy && Style.animations
                            loops: Animation.Infinite
                            from: 0; to: 360
                            duration: 900
                        }
                    }
                    Text {
                        width: 220
                        text: page.bc.status
                        font.pixelSize: Style.fontSmall + 1
                        color: Style.onSlate
                        wrapMode: Text.Wrap
                    }
                }
                Text {
                    text: "Battle of " + page.bc.cityName
                    font.pixelSize: Style.fontSmall
                    color: Style.onSlateFaint
                }

                PipDivider { Layout.fillWidth: true }

                Party { info: page.enemy; side: page.enemySide; mine: false }
                Text {
                    text: "▲  the enemy holds the top"
                    font.pixelSize: Style.fontSmall
                    color: Style.onSlateFaint
                }

                Item { Layout.fillHeight: true }

                Text {
                    Layout.fillWidth: true
                    text: page.bc.phase === 0 ? "Click one of your units, then a cell in your zone, to move or swap it. Ready when the formation suits you."
                        : page.bc.phase === 1 ? "Click your units to crown " + page.bc.generalsRequired(page.bc.viewSide) + " general" + (page.bc.generalsRequired(page.bc.viewSide) === 1 ? "" : "s") + " (" + page.bc.generalsPromoted(page.bc.viewSide) + " chosen). A crowned unit becomes a soldier for good."
                        : page.bc.phase === 2 ? "Click a unit, then a green cell to move or a red one to strike. Right-click a unit for its card."
                        : ""
                    font.pixelSize: Style.fontSmall
                    color: Style.onSlateFaint
                    wrapMode: Text.Wrap
                }

                Item { Layout.fillHeight: true }

                Text {
                    text: "▼  you hold the bottom"
                    font.pixelSize: Style.fontSmall
                    color: Style.onSlateFaint
                }
                Party { info: page.me; side: page.mySide; mine: true }
            }
        }

        // -- the board -------------------------------------------------------------
        Item {
            id: boardArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                id: board
                width: page.across * page.cell
                height: page.length * page.cell
                anchors.centerIn: parent

                SequentialAnimation {
                    id: shake
                    loops: 3
                    NumberAnimation { target: board; property: "anchors.horizontalCenterOffset"; to: 7; duration: 40 }
                    NumberAnimation { target: board; property: "anchors.horizontalCenterOffset"; to: -7; duration: 40 }
                    NumberAnimation { target: board; property: "anchors.horizontalCenterOffset"; to: 0; duration: 40 }
                }

                Repeater {
                    model: page.length * page.across
                    BoardCell {
                        id: square
                        required property int index
                        readonly property int bx: Math.floor(index / page.across)
                        readonly property int by: index % page.across
                        x: page.screenCol(by) * page.cell
                        y: page.screenRow(bx) * page.cell
                        width: page.cell
                        height: page.cell
                        dark: (bx + by) % 2 === 0
                        zone: page.bc.inZone(0, bx, by) ? 0 : page.bc.inZone(1, bx, by) ? 1 : -1
                        zoneColor: zone === 0 ? page.bc.attacker.color : page.bc.defender.color
                        highlight: page.highlightMap[bx + "," + by] || 0
                        hovered: page.hoverX === bx && page.hoverY === by
                        cursor: page.cursorCol === page.screenCol(by) && page.cursorRow === page.screenRow(bx)
                        HoverHandler {
                            onHoveredChanged: {
                                if (hovered) { page.hoverX = square.bx; page.hoverY = square.by }
                                else if (page.hoverX === square.bx && page.hoverY === square.by) { page.hoverX = -1; page.hoverY = -1 }
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: { page.cursorCol = -1; page.bc.cellClicked(square.bx, square.by) }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: page.showCard(square.bx, square.by)
                        }
                    }
                }

                // the edge labels: the enemy above, the player below
                Text {
                    anchors.bottom: parent.top
                    anchors.bottomMargin: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (page.enemy.name || "").toUpperCase()
                    font.family: Style.displayFamily
                    font.pixelSize: Style.fontSmall + 1
                    font.letterSpacing: 3
                    color: Qt.alpha(page.enemy.color || Style.brass, 0.95)
                }
                Text {
                    anchors.top: parent.bottom
                    anchors.topMargin: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (page.me.name || "").toUpperCase()
                    font.family: Style.displayFamily
                    font.pixelSize: Style.fontSmall + 1
                    font.letterSpacing: 3
                    color: Qt.alpha(page.me.color || Style.brass, 0.95)
                }

                Repeater {
                    model: page.bc.units
                    UnitChip {
                        required property var model
                        x: page.screenCol(model.y) * page.cell
                        y: page.screenRow(model.x) * page.cell
                        width: page.cell
                        height: page.cell
                        icon: model.icon
                        banner: page.sideColor(model.side)
                        general: model.general
                        acted: model.acted && page.bc.phase === 2
                        alive: model.alive
                        selected: page.bc.selectedX === model.x && page.bc.selectedY === model.y
                        mine: page.bc.isHuman(model.side)
                        z: alive ? 1 : 2
                    }
                }

                Projectile {
                    id: shot
                    anchors.fill: parent
                    z: 5
                    onLanded: {
                        const a = page.pendingStrike
                        page.pendingStrike = null
                        if (!a) return
                        if (a.victimTypeKey) Audio.playUnit(a.victimTypeKey, "explosion")
                        if (a.victimGeneral && Style.combatShake) shake.restart()
                    }
                }
            }
        }

        // -- the orders ------------------------------------------------------------
        Panel {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            title: "Orders"
            padding: 12

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                // actions left this turn
                Row {
                    visible: page.bc.phase === 2
                    spacing: 5
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Actions"
                        font.pixelSize: Style.fontSmall
                        color: Style.onSlateFaint
                    }
                    Repeater {
                        model: Math.max(page.bc.actionsLeft, page.bc.sideToAct === 0 ? page.bc.attackerGenerals : page.bc.defenderGenerals)
                        Rectangle {
                            required property int index
                            anchors.verticalCenter: parent.verticalCenter
                            width: 10; height: 10; radius: 5
                            color: index < page.bc.actionsLeft ? Style.lamp : "transparent"
                            border.width: 1
                            border.color: Style.brassDark
                        }
                    }
                }

                Button {
                    Layout.fillWidth: true
                    visible: page.bc.phase < 2
                    text: "Ready"
                    primary: true
                    enabled: page.bc.canReady
                    onClicked: page.bc.ready()
                    ToolTip.visible: hovered
                    ToolTip.text: "Enter"
                }
                Button {
                    Layout.fillWidth: true
                    visible: page.bc.phase === 2
                    text: "End turn"
                    primary: true
                    enabled: page.bc.myTurn
                    onClicked: page.bc.endTurn()
                    ToolTip.visible: hovered
                    ToolTip.text: "Enter"
                }
                Button {
                    Layout.fillWidth: true
                    visible: page.bc.phase === 2
                    text: page.bc.drawOfferedByEnemy ? "Accept draw" : page.bc.drawOfferedByMe ? "Draw offered" : "Offer draw"
                    enabled: page.bc.myTurn && !page.bc.drawOfferedByMe
                    highlighted: page.bc.drawOfferedByEnemy
                    onClicked: page.bc.offerDraw()
                    ToolTip.visible: hovered
                    ToolTip.text: "D"
                }
                Button {
                    Layout.fillWidth: true
                    visible: page.bc.phase === 2
                    text: "Surrender"
                    danger: true
                    enabled: !page.bc.over && !page.bc.busy
                    onClicked: surrenderDialog.open()
                }

                Item { Layout.fillHeight: true }

                Button {
                    Layout.fillWidth: true
                    visible: !GameController.online
                    text: "Auto-resolve"
                    enabled: !page.bc.over && !page.bc.busy
                    onClicked: page.bc.autoResolve()
                    ToolTip.visible: hovered
                    ToolTip.text: "The engine plays both sides from here"
                }
                Button {
                    Layout.fillWidth: true
                    text: "Quit"
                    // a watcher has nothing to concede; the board plays out on the other machine
                    enabled: !page.bc.over && !page.bc.busy && page.bc.isHuman(page.mySide)
                    onClicked: quitDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: "Escape"
                }
                Column {
                    Layout.fillWidth: true
                    spacing: 4
                    Row { spacing: 6; KeyHint { key: "Enter" } Text { text: "ready / end turn"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall; anchors.verticalCenter: parent.verticalCenter } }
                    Row { spacing: 6; KeyHint { key: "D" } Text { text: "offer a draw"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall; anchors.verticalCenter: parent.verticalCenter } }
                    Row { spacing: 6; KeyHint { key: "↑↓←→ ␣" } Text { text: "cursor, act"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall; anchors.verticalCenter: parent.verticalCenter } }
                }
            }
        }
    }

    UnitCard { id: unitCard }

    ConfirmDialog {
        id: surrenderDialog
        title: "Surrender?"
        text: "Your side loses the battle and every unit on the board."
        confirmText: "Surrender"
        destructive: true
        onAccepted: page.bc.surrender()
    }
    ConfirmDialog {
        id: quitDialog
        title: "Quit the battle?"
        text: page.bc.phase === 2 ? "Your side concedes the battle as it stands."
            : page.mySide === 0 ? "The attackers withdraw and nothing is lost." : "The defenders abandon the city and nothing is lost."
        confirmText: "Quit"
        destructive: true
        onAccepted: page.bc.quit()
    }

    BattleResult {
        anchors.fill: parent
        onLeave: page.bc.leave()
    }
}
