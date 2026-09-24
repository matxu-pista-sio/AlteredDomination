pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Layouts
import AD
import AD.Theme
import AD.Components

// The board battle (docs/GAME_DESIGN.md §8, docs/UI_THEME.md "The battle
// screen"): the tactical grid projected on the table, the chips, the
// projectile and sparks of a strike, the phases' controls. Every action
// goes through GameController.battle; the human's side is drawn on the
// left.
FocusScope {
    id: page

    signal finished()

    focus: true

    readonly property var bc: GameController.battle
    readonly property int length: page.bc.boardLength
    readonly property int rows: page.bc.boardWidth
    readonly property bool mirrored: page.bc.viewSide === 1
    readonly property int cell: Math.max(26, Math.floor(Math.min((boardArea.width - 32) / length,
                                                                (boardArea.height - 32) / rows)))
    readonly property var leftParty: mirrored ? page.bc.defender : page.bc.attacker
    readonly property var rightParty: mirrored ? page.bc.attacker : page.bc.defender
    readonly property var highlightMap: {
        const m = {}
        for (const h of page.bc.highlights) m[h.x + "," + h.y] = h.kind
        return m
    }
    property int cursorX: -1
    property int cursorY: -1
    property int hoverX: -1
    property int hoverY: -1
    property var pendingStrike: null

    function sx(x) { return mirrored ? length - 1 - x : x }
    function centerOf(x, y) { return Qt.point(sx(x) * cell + cell / 2, y * cell + cell / 2) }
    function sideColor(side) { return side === 0 ? page.bc.attacker.color : page.bc.defender.color }
    function moveCursor(dx, dy) {
        if (cursorX < 0) { cursorX = mirrored ? length - 1 : 0; cursorY = 0; return }
        cursorX = Math.max(0, Math.min(length - 1, cursorX + (mirrored ? -dx : dx)))
        cursorY = Math.max(0, Math.min(rows - 1, cursorY + dy))
    }
    function showCard(x, y) {
        const u = page.bc.unitAt(x, y)
        if (!u.typeKey) return
        unitCard.info = GameController.catalog.info(u.typeKey)
        unitCard.state = (u.side === 0 ? page.bc.attacker.name : page.bc.defender.name)
                         + (u.general ? "  ·  general" : "") + (u.acted ? "  ·  has acted this turn" : "")
        unitCard.x = Math.min(page.width - unitCard.width - 12, boardArea.x + board.x + sx(x) * cell + cell + 8)
        unitCard.y = Math.min(page.height - unitCard.height - 12, boardArea.y + board.y + y * cell)
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
        case Qt.Key_Space: if (cursorX >= 0) page.bc.cellClicked(cursorX, cursorY); break
        default: return
        }
        event.accepted = true
    }

    Component.onCompleted: page.forceActiveFocus()
    StackView.onActivated: page.forceActiveFocus()

    Rectangle { anchors.fill: parent; color: Style.slate }

    // the lamp's pool on the table
    Rectangle {
        anchors.centerIn: boardArea
        width: boardArea.width * 1.2
        height: boardArea.height * 1.3
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // -- header -------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            padding: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12

                Flag { Layout.preferredWidth: 44; Layout.preferredHeight: 33; source: page.leftParty.flag || "" }
                Column {
                    Text {
                        text: (page.leftParty.name || "") + (page.bc.isHuman(page.mirrored ? 1 : 0) ? "  (you)" : "")
                        font.family: Style.displayFamily
                        font.pixelSize: Style.fontBody + 4
                        font.bold: true
                        color: Style.onSlate
                    }
                    Text {
                        readonly property int units: page.mirrored ? page.bc.defenderUnits : page.bc.attackerUnits
                        readonly property int generals: page.mirrored ? page.bc.defenderGenerals : page.bc.attackerGenerals
                        readonly property int power: page.mirrored ? page.bc.defenderPower : page.bc.attackerPower
                        text: units + " units  ·  power " + power + "  ·  " + generals + " general" + (generals === 1 ? "" : "s")
                        font.pixelSize: Style.fontSmall
                        font.features: { "tnum": 1 }
                        color: Style.onSlateFaint
                    }
                }

                Item { Layout.fillWidth: true }

                Column {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 2
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: page.bc.phase === 0 ? "DEPLOY" : page.bc.phase === 1 ? "PROMOTE" : page.bc.phase === 2 ? "TURN " + page.bc.turn : "BATTLE OVER"
                        font.family: Style.displayFamily
                        font.pixelSize: Style.fontTitle
                        font.bold: true
                        font.letterSpacing: 3
                        color: Style.brassBright
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 8
                        // the thinking spinner
                        Item {
                            width: 16; height: 16
                            visible: page.bc.busy
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle {
                                anchors.fill: parent
                                radius: 8
                                color: "transparent"
                                border.width: 2
                                border.color: Style.lamp
                                opacity: 0.35
                            }
                            Rectangle {
                                width: 6; height: 6; radius: 3
                                x: 5; y: -1
                                color: Style.lamp
                                transformOrigin: Item.Center
                            }
                            RotationAnimation on rotation {
                                running: page.bc.busy && Style.animations
                                loops: Animation.Infinite
                                from: 0; to: 360
                                duration: 900
                            }
                        }
                        Text {
                            text: page.bc.status + "  ·  " + page.bc.cityName
                            font.pixelSize: Style.fontSmall + 1
                            color: Style.onSlate
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Column {
                    Text {
                        anchors.right: parent.right
                        text: (page.rightParty.name || "") + (page.bc.isHuman(page.mirrored ? 0 : 1) ? "  (you)" : "")
                        font.family: Style.displayFamily
                        font.pixelSize: Style.fontBody + 4
                        font.bold: true
                        color: Style.onSlate
                    }
                    Text {
                        anchors.right: parent.right
                        readonly property int units: page.mirrored ? page.bc.attackerUnits : page.bc.defenderUnits
                        readonly property int generals: page.mirrored ? page.bc.attackerGenerals : page.bc.defenderGenerals
                        readonly property int power: page.mirrored ? page.bc.attackerPower : page.bc.defenderPower
                        text: units + " units  ·  power " + power + "  ·  " + generals + " general" + (generals === 1 ? "" : "s")
                        font.pixelSize: Style.fontSmall
                        font.features: { "tnum": 1 }
                        color: Style.onSlateFaint
                    }
                }
                Flag { Layout.preferredWidth: 44; Layout.preferredHeight: 33; source: page.rightParty.flag || "" }
            }
        }

        // -- the board ------------------------------------------------------------
        Item {
            id: boardArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                id: board
                width: page.length * page.cell
                height: page.rows * page.cell
                anchors.centerIn: parent

                SequentialAnimation {
                    id: shake
                    loops: 3
                    NumberAnimation { target: board; property: "anchors.horizontalCenterOffset"; to: 7; duration: 40 }
                    NumberAnimation { target: board; property: "anchors.horizontalCenterOffset"; to: -7; duration: 40 }
                    NumberAnimation { target: board; property: "anchors.horizontalCenterOffset"; to: 0; duration: 40 }
                }

                Repeater {
                    model: page.length * page.rows
                    BoardCell {
                        id: square
                        required property int index
                        readonly property int bx: index % page.length
                        readonly property int by: Math.floor(index / page.length)
                        x: page.sx(bx) * page.cell
                        y: by * page.cell
                        width: page.cell
                        height: page.cell
                        dark: (bx + by) % 2 === 0
                        zone: page.bc.inZone(0, bx, by) ? 0 : page.bc.inZone(1, bx, by) ? 1 : -1
                        zoneColor: zone === 0 ? page.bc.attacker.color : page.bc.defender.color
                        highlight: page.highlightMap[bx + "," + by] || 0
                        hovered: page.hoverX === bx && page.hoverY === by
                        cursor: page.cursorX === bx && page.cursorY === by
                        HoverHandler {
                            onHoveredChanged: {
                                if (hovered) { page.hoverX = square.bx; page.hoverY = square.by }
                                else if (page.hoverX === square.bx && page.hoverY === square.by) { page.hoverX = -1; page.hoverY = -1 }
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: { page.cursorX = -1; page.bc.cellClicked(square.bx, square.by) }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: page.showCard(square.bx, square.by)
                        }
                    }
                }

                // the edge labels
                Text {
                    anchors.bottom: parent.top
                    anchors.bottomMargin: 4
                    x: 2
                    text: (page.leftParty.name || "").toUpperCase()
                    font.family: Style.displayFamily
                    font.pixelSize: Style.fontSmall
                    font.letterSpacing: 2
                    color: Qt.alpha(page.leftParty.color || Style.brass, 0.9)
                }
                Text {
                    anchors.bottom: parent.top
                    anchors.bottomMargin: 4
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    text: (page.rightParty.name || "").toUpperCase()
                    font.family: Style.displayFamily
                    font.pixelSize: Style.fontSmall
                    font.letterSpacing: 2
                    color: Qt.alpha(page.rightParty.color || Style.brass, 0.9)
                }

                Repeater {
                    model: page.bc.units
                    UnitChip {
                        required property var model
                        x: page.sx(model.x) * page.cell
                        y: model.y * page.cell
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

        // -- the action bar ----------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            padding: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: page.bc.phase === 0 ? "Click one of your units, then a cell in your zone, to move or swap it. Ready when the formation suits you."
                        : page.bc.phase === 1 ? "Click your units to crown " + page.bc.generalsRequired(page.bc.viewSide) + " general" + (page.bc.generalsRequired(page.bc.viewSide) === 1 ? "" : "s") + " (" + page.bc.generalsPromoted(page.bc.viewSide) + " chosen). A crowned unit becomes a soldier for good."
                        : page.bc.phase === 2 ? "Click a unit, then a green cell to move or a red one to strike. Right-click a unit for its card."
                        : ""
                    font.pixelSize: Style.fontSmall + 1
                    color: Style.onSlateFaint
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                // actions left
                Row {
                    visible: page.bc.phase === 2
                    spacing: 4
                    Repeater {
                        model: Math.max(page.bc.actionsLeft, page.bc.sideToAct === 0 ? page.bc.attackerGenerals : page.bc.defenderGenerals)
                        Rectangle {
                            required property int index
                            width: 10; height: 10; radius: 5
                            color: index < page.bc.actionsLeft ? Style.lamp : "transparent"
                            border.width: 1
                            border.color: Style.brassDark
                        }
                    }
                }

                Button {
                    visible: page.bc.phase < 2
                    text: "Ready"
                    primary: true
                    enabled: page.bc.canReady
                    onClicked: page.bc.ready()
                    ToolTip.visible: hovered
                    ToolTip.text: "Enter"
                }
                Button {
                    visible: page.bc.phase === 2
                    text: "End turn"
                    primary: true
                    enabled: page.bc.myTurn
                    onClicked: page.bc.endTurn()
                    ToolTip.visible: hovered
                    ToolTip.text: "Enter"
                }
                Button {
                    visible: page.bc.phase === 2
                    text: page.bc.drawOfferedByEnemy ? "Accept draw" : page.bc.drawOfferedByMe ? "Draw offered" : "Offer draw"
                    enabled: page.bc.myTurn && !page.bc.drawOfferedByMe
                    highlighted: page.bc.drawOfferedByEnemy
                    onClicked: page.bc.offerDraw()
                    ToolTip.visible: hovered
                    ToolTip.text: "D"
                }
                Button {
                    visible: page.bc.phase === 2
                    text: "Surrender"
                    danger: true
                    enabled: !page.bc.over && !page.bc.busy
                    onClicked: surrenderDialog.open()
                }
                Button {
                    text: "Auto-resolve"
                    enabled: !page.bc.over && !page.bc.busy
                    onClicked: page.bc.autoResolve()
                    ToolTip.visible: hovered
                    ToolTip.text: "The engine plays both sides from here"
                }
                Button {
                    text: "Quit"
                    enabled: !page.bc.over && !page.bc.busy
                    onClicked: quitDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: "Escape"
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
        text: page.bc.phase === 2 ? "Your side concedes the battle as it stands." : "The attackers withdraw and nothing is lost."
        confirmText: "Quit"
        destructive: true
        onAccepted: page.bc.quit()
    }

    BattleResult {
        anchors.fill: parent
        onLeave: page.bc.leave()
    }
}
