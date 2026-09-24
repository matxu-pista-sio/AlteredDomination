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
    readonly property int length: bc.boardLength
    readonly property int rows: bc.boardWidth
    readonly property bool mirrored: bc.viewSide === 1
    readonly property int cell: Math.max(26, Math.floor(Math.min((boardArea.width - 32) / length,
                                                                (boardArea.height - 32) / rows)))
    readonly property var leftParty: mirrored ? bc.defender : bc.attacker
    readonly property var rightParty: mirrored ? bc.attacker : bc.defender
    readonly property var highlightMap: {
        const m = {}
        for (const h of bc.highlights) m[h.x + "," + h.y] = h.kind
        return m
    }
    property int cursorX: -1
    property int cursorY: -1
    property int hoverX: -1
    property int hoverY: -1
    property var pendingStrike: null

    function sx(x) { return mirrored ? length - 1 - x : x }
    function centerOf(x, y) { return Qt.point(sx(x) * cell + cell / 2, y * cell + cell / 2) }
    function sideColor(side) { return side === 0 ? bc.attacker.color : bc.defender.color }
    function moveCursor(dx, dy) {
        if (cursorX < 0) { cursorX = mirrored ? length - 1 : 0; cursorY = 0; return }
        cursorX = Math.max(0, Math.min(length - 1, cursorX + (mirrored ? -dx : dx)))
        cursorY = Math.max(0, Math.min(rows - 1, cursorY + dy))
    }
    function showCard(x, y) {
        const u = bc.unitAt(x, y)
        if (!u.typeKey) return
        unitCard.info = GameController.catalog.info(u.typeKey)
        unitCard.state = (u.side === 0 ? bc.attacker.name : bc.defender.name)
                         + (u.general ? "  ·  general" : "") + (u.acted ? "  ·  has acted this turn" : "")
        unitCard.x = Math.min(page.width - unitCard.width - 12, boardArea.x + board.x + sx(x) * cell + cell + 8)
        unitCard.y = Math.min(page.height - unitCard.height - 12, boardArea.y + board.y + y * cell)
        unitCard.open()
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Escape:
            if (unitCard.opened) unitCard.close()
            else if (bc.selectedX >= 0) bc.clearSelection()
            else if (bc.over) bc.leave()
            else quitDialog.open()
            break
        case Qt.Key_Return: case Qt.Key_Enter:
            if (bc.over) bc.leave()
            else if (bc.canReady) bc.ready()
            else if (bc.myTurn) bc.endTurn()
            break
        case Qt.Key_D: if (bc.myTurn) bc.offerDraw(); break
        case Qt.Key_Left: moveCursor(-1, 0); break
        case Qt.Key_Right: moveCursor(1, 0); break
        case Qt.Key_Up: moveCursor(0, -1); break
        case Qt.Key_Down: moveCursor(0, 1); break
        case Qt.Key_Space: if (cursorX >= 0) bc.cellClicked(cursorX, cursorY); break
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
                        text: (page.leftParty.name || "") + (bc.isHuman(page.mirrored ? 1 : 0) ? "  (you)" : "")
                        font.family: Style.displayFamily
                        font.pixelSize: Style.fontBody + 4
                        font.bold: true
                        color: Style.onSlate
                    }
                    Text {
                        readonly property int units: page.mirrored ? bc.defenderUnits : bc.attackerUnits
                        readonly property int generals: page.mirrored ? bc.defenderGenerals : bc.attackerGenerals
                        readonly property int power: page.mirrored ? bc.defenderPower : bc.attackerPower
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
                        text: bc.phase === 0 ? "DEPLOY" : bc.phase === 1 ? "PROMOTE" : bc.phase === 2 ? "TURN " + bc.turn : "BATTLE OVER"
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
                            visible: bc.busy
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
                                running: bc.busy && Style.animations
                                loops: Animation.Infinite
                                from: 0; to: 360
                                duration: 900
                            }
                        }
                        Text {
                            text: bc.status + "  ·  " + bc.cityName
                            font.pixelSize: Style.fontSmall + 1
                            color: Style.onSlate
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Column {
                    Text {
                        anchors.right: parent.right
                        text: (page.rightParty.name || "") + (bc.isHuman(page.mirrored ? 0 : 1) ? "  (you)" : "")
                        font.family: Style.displayFamily
                        font.pixelSize: Style.fontBody + 4
                        font.bold: true
                        color: Style.onSlate
                    }
                    Text {
                        anchors.right: parent.right
                        readonly property int units: page.mirrored ? bc.attackerUnits : bc.defenderUnits
                        readonly property int generals: page.mirrored ? bc.attackerGenerals : bc.defenderGenerals
                        readonly property int power: page.mirrored ? bc.attackerPower : bc.defenderPower
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
                        zone: bc.inZone(0, bx, by) ? 0 : bc.inZone(1, bx, by) ? 1 : -1
                        zoneColor: zone === 0 ? bc.attacker.color : bc.defender.color
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
                            onTapped: { page.cursorX = -1; bc.cellClicked(square.bx, square.by) }
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
                    model: bc.units
                    UnitChip {
                        required property var model
                        x: page.sx(model.x) * page.cell
                        y: model.y * page.cell
                        width: page.cell
                        height: page.cell
                        icon: model.icon
                        banner: page.sideColor(model.side)
                        general: model.general
                        acted: model.acted && bc.phase === 2
                        alive: model.alive
                        selected: bc.selectedX === model.x && bc.selectedY === model.y
                        mine: bc.isHuman(model.side)
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
                    text: bc.phase === 0 ? "Click one of your units, then a cell in your zone, to move or swap it. Ready when the formation suits you."
                        : bc.phase === 1 ? "Click your units to crown " + bc.generalsRequired(bc.viewSide) + " general" + (bc.generalsRequired(bc.viewSide) === 1 ? "" : "s") + " (" + bc.generalsPromoted(bc.viewSide) + " chosen). A crowned unit becomes a soldier for good."
                        : bc.phase === 2 ? "Click a unit, then a green cell to move or a red one to strike. Right-click a unit for its card."
                        : ""
                    font.pixelSize: Style.fontSmall + 1
                    color: Style.onSlateFaint
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                // actions left
                Row {
                    visible: bc.phase === 2
                    spacing: 4
                    Repeater {
                        model: Math.max(bc.actionsLeft, bc.sideToAct === 0 ? bc.attackerGenerals : bc.defenderGenerals)
                        Rectangle {
                            required property int index
                            width: 10; height: 10; radius: 5
                            color: index < bc.actionsLeft ? Style.lamp : "transparent"
                            border.width: 1
                            border.color: Style.brassDark
                        }
                    }
                }

                Button {
                    visible: bc.phase < 2
                    text: "Ready"
                    primary: true
                    enabled: bc.canReady
                    onClicked: bc.ready()
                    ToolTip.visible: hovered
                    ToolTip.text: "Enter"
                }
                Button {
                    visible: bc.phase === 2
                    text: "End turn"
                    primary: true
                    enabled: bc.myTurn
                    onClicked: bc.endTurn()
                    ToolTip.visible: hovered
                    ToolTip.text: "Enter"
                }
                Button {
                    visible: bc.phase === 2
                    text: bc.drawOfferedByEnemy ? "Accept draw" : bc.drawOfferedByMe ? "Draw offered" : "Offer draw"
                    enabled: bc.myTurn && !bc.drawOfferedByMe
                    highlighted: bc.drawOfferedByEnemy
                    onClicked: bc.offerDraw()
                    ToolTip.visible: hovered
                    ToolTip.text: "D"
                }
                Button {
                    visible: bc.phase === 2
                    text: "Surrender"
                    danger: true
                    enabled: !bc.over && !bc.busy
                    onClicked: surrenderDialog.open()
                }
                Button {
                    text: "Auto-resolve"
                    enabled: !bc.over && !bc.busy
                    onClicked: bc.autoResolve()
                    ToolTip.visible: hovered
                    ToolTip.text: "The engine plays both sides from here"
                }
                Button {
                    text: "Quit"
                    enabled: !bc.over && !bc.busy
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
        onAccepted: bc.surrender()
    }
    ConfirmDialog {
        id: quitDialog
        title: "Quit the battle?"
        text: bc.phase === 2 ? "Your side concedes the battle as it stands." : "The attackers withdraw and nothing is lost."
        confirmText: "Quit"
        destructive: true
        onAccepted: bc.quit()
    }

    BattleResult {
        anchors.fill: parent
        onLeave: bc.leave()
    }
}
