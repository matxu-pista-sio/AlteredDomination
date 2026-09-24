pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import AD
import AD.Theme
import AD.Components

// The campaign: the chart, and over it the top bar, the ranking, the city
// sheet, the force picker, the battle prompt, the menu and the end. Every
// action goes through GameController; this page only asks.
FocusScope {
    id: page

    signal menu()
    signal openSettings()

    focus: true

    readonly property bool sheetOpen: GameController.selectedCity >= 0
    property string seenPlayer: ""

    function cityTapped(id) {
        const mode = GameController.interaction
        const from = GameController.selectedCity
        if (mode !== GameController.Browse && from >= 0 && id !== from) {
            const targets = mode === GameController.Moving ? GameController.moveTargets(from)
                                                            : GameController.attackTargets(from)
            for (const t of targets)
                if (t.id === id) { forcePicker.openFor(mode, from, t); return }
        }
        GameController.selectedCity = id
        Audio.play("hover")
    }
    function endTurn() {
        if (!GameController.humanTurn) return
        Audio.play("click")
        GameController.endTurn()
    }
    function cycleCity(forward) {
        const cities = GameController.citiesWithUnacted()
        if (cities.length === 0) return
        let i = cities.indexOf(GameController.selectedCity)
        i = i < 0 ? 0 : (i + (forward ? 1 : cities.length - 1)) % cities.length
        GameController.selectedCity = cities[i]
        map.flyTo(cities[i])
    }
    function goBack() {
        if (GameController.interaction !== GameController.Browse) { GameController.interaction = GameController.Browse; return }
        if (citySheet.recruiting) { citySheet.recruiting = false; return }
        if (GameController.selectedCity >= 0) { GameController.selectedCity = -1; return }
        gameMenu.open()
    }

    Keys.onPressed: (event) => {
        const step = 90
        switch (event.key) {
        case Qt.Key_Escape: goBack(); break
        case Qt.Key_E: endTurn(); break
        case Qt.Key_F: citySearch.open(); break
        case Qt.Key_Tab: cycleCity(true); break
        case Qt.Key_Backtab: cycleCity(false); break
        case Qt.Key_Left: map.panBy(step, 0); break
        case Qt.Key_Right: map.panBy(-step, 0); break
        case Qt.Key_Up: map.panBy(0, step); break
        case Qt.Key_Down: map.panBy(0, -step); break
        case Qt.Key_Plus: case Qt.Key_Equal: map.zoomAt(map.width / 2, map.height / 2, 1.4); break
        case Qt.Key_Minus: map.zoomAt(map.width / 2, map.height / 2, 1 / 1.4); break
        case Qt.Key_0: map.fitWorld(); break
        case Qt.Key_Home: map.flyTo(GameController.capitalOf(GameController.humanKey)); break
        case Qt.Key_R: if (citySheet.canAct) { citySheet.recruiting = !citySheet.recruiting; GameController.interaction = GameController.Browse } break
        case Qt.Key_M: if (citySheet.canAct) { citySheet.recruiting = false; GameController.interaction = GameController.interaction === GameController.Moving ? GameController.Browse : GameController.Moving } break
        case Qt.Key_A: if (citySheet.canAct) { citySheet.recruiting = false; GameController.interaction = GameController.interaction === GameController.Attacking ? GameController.Browse : GameController.Attacking } break
        case Qt.Key_S: if (event.modifiers & Qt.ControlModifier) gameMenu.quickSave(); else return; break
        default: return
        }
        event.accepted = true
    }

    Component.onCompleted: {
        map.fitWorld()
        page.seenPlayer = GameController.currentPlayerKey
        page.forceActiveFocus()
        arrival.start()
    }
    // the page regains the keyboard whenever it becomes the current one
    onVisibleChanged: if (visible) page.forceActiveFocus()
    StackView.onActivated: page.forceActiveFocus()
    Timer {
        id: arrival
        interval: 450
        onTriggered: map.flyTo(GameController.capitalOf(GameController.humanKey))
    }

    WorldMap {
        id: map
        anchors.fill: parent
        onCityClicked: (id) => page.cityTapped(id)
        onCityRightClicked: (id) => { GameController.selectedCity = id; map.flyTo(id) }
        onEmptyClicked: {
            if (GameController.interaction !== GameController.Browse) GameController.interaction = GameController.Browse
            else GameController.selectedCity = -1
        }
    }

    // the hovered city's name, for the cities whose label is hidden by the LOD
    Rectangle {
        visible: map.hoveredCity >= 0 && !page.sheetOpen
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        width: hoverLabel.implicitWidth + 24
        height: 28
        radius: Style.radius
        color: Qt.alpha(Style.slateRaised, 0.9)
        border.width: 1
        border.color: Style.brassDark
        readonly property var info: map.hoveredCity >= 0 ? GameController.cityInfo(map.hoveredCity) : {}
        Text {
            id: hoverLabel
            anchors.centerIn: parent
            text: (parent.info.name || "") + "  ·  " + (parent.info.ownerName || "") + "  ·  power " + (parent.info.power || 0)
            font.pixelSize: Style.fontSmall + 1
            color: Style.onSlate
        }
    }

    TopBar {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        onEndTurn: page.endTurn()
        onMenu: gameMenu.open()
        onFindCity: citySearch.open()
    }

    RankingPanel {
        id: ranking
        anchors.left: parent.left
        anchors.top: topBar.bottom
        anchors.margins: 12
        backdrop: map
    }

    CitySheet {
        id: citySheet
        anchors.right: parent.right
        anchors.top: topBar.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 12
        backdrop: map
        visible: opacity > 0
        opacity: page.sheetOpen ? 1 : 0
        anchors.rightMargin: page.sheetOpen ? 12 : -40
        Behavior on opacity { enabled: Style.animations; NumberAnimation { duration: 180 } }
        Behavior on anchors.rightMargin { enabled: Style.animations; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        onFlyTo: (id) => map.flyTo(id)
        onOpenForce: (mode, from, target) => forcePicker.openFor(mode, from, target)
        onFailed: (text) => GameController.notice(text, "bad")
    }

    // A closed popup hands the keyboard back to the page.
    ForcePicker {
        id: forcePicker
        onFailed: (text) => GameController.notice(text, "bad")
        onDone: (result) => { if (result === "captured") Audio.play("explosion") }
        onClosed: page.forceActiveFocus()
    }
    BattlePrompt {
        id: battlePrompt
        onClosed: page.forceActiveFocus()
    }
    CitySearch {
        id: citySearch
        onPicked: (id) => { GameController.selectedCity = id; map.flyTo(id) }
        onClosed: page.forceActiveFocus()
    }
    InGameMenu {
        id: gameMenu
        onOpenSettings: page.openSettings()
        onQuitToMenu: page.menu()
        onClosed: page.forceActiveFocus()
    }
    GameOverOverlay {
        anchors.fill: parent
        onMenu: page.menu()
    }

    // online: the opponent dropped; the server holds the match for a while
    Rectangle {
        visible: GameController.online && !GameController.peerConnected && !GameController.onlineOver
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: topBar.bottom
        anchors.topMargin: 10
        width: dropLabel.implicitWidth + 28
        height: 32
        radius: Style.radius
        color: Qt.alpha(Style.danger, 0.85)
        Text {
            id: dropLabel
            anchors.centerIn: parent
            text: GameController.opponentName + " lost the connection - the match waits up to " + GameController.peerGraceSeconds + " s for them"
            font.pixelSize: Style.fontSmall + 1
            color: Style.paper
        }
    }

    // hotseat: the table changes hands
    Rectangle {
        id: handover
        anchors.fill: parent
        color: Qt.alpha(Style.ink, 0.85)
        visible: false
        MouseArea { anchors.fill: parent }
        Column {
            anchors.centerIn: parent
            spacing: 16
            Flag { anchors.horizontalCenter: parent.horizontalCenter; width: 120; height: 90; source: GameController.humanFlag }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: GameController.humanName + " to play"
                font.family: Style.displayFamily
                font.pixelSize: Style.fontDisplay
                font.bold: true
                color: Style.paper
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Pass the table. Round " + GameController.round
                font.pixelSize: Style.fontBody + 1
                color: Style.onSlateFaint
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Ready"
                primary: true
                width: 160
                height: 40
                onClicked: { handover.visible = false; map.flyTo(GameController.capitalOf(GameController.humanKey)) }
            }
        }
    }

    Connections {
        target: GameController
        function onStateChanged() {
            if (GameController.hotseat && GameController.humanTurn && GameController.currentPlayerKey !== page.seenPlayer) {
                page.seenPlayer = GameController.currentPlayerKey
                handover.visible = true
            }
        }
        function onRoundEnded(round) {
            GameController.notice("Round " + (round + 1) + " · income paid, " + GameController.formatNumber(GameController.funds) + " in the treasury", "info")
        }
        function onCityCaptured(cityId, byKey) {
            if (byKey === GameController.humanKey) Audio.play("explosion")
        }
        function onGameEnded(victory) {
            Audio.play(victory ? "hoverhome" : "explosion")
        }
        function onOnlineChanged() {
            if (GameController.onlineOver) Audio.play(GameController.onlineOutcome.won ? "hoverhome" : "explosion")
        }
    }
}
