pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtCore
import AD
import AD.Theme
import AD.Components

// The one window (docs/ARCHITECTURE.md): a StackView of pages over the
// core bridge. Nothing here knows a rule; every page talks to
// GameController and the pages talk to each other through signals.
ApplicationWindow {
    id: root

    width: 1280
    height: 800
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: "Altered Domination"
    color: Style.slate
    font.family: Style.bodyFamily
    font.pixelSize: Style.fontBody

    // for the headless driver's `eval` (devdrive.h): the controllers by name
    readonly property QtObject game: GameController
    readonly property QtObject net: LobbyClient

    Settings {
        id: windowSettings
        category: "window"
        property alias width: root.width
        property alias height: root.height
    }

    // -- navigation ---------------------------------------------------------------
    function toHome() {
        stack.clear()
        stack.push(homePage)
        Audio.music("menu")
    }
    function open(page) { stack.push(page) }
    function back() { if (stack.depth > 1) stack.pop() }
    function startCampaign() {
        stack.clear()
        stack.push(campaignPage)
        Audio.music("map")
    }
    function leaveCampaign() {
        GameController.leaveGame()
        toHome()
    }

    // The headless driver's `page <name>` (devdrive.h): jump anywhere.
    function devPage(name) {
        switch (name) {
        case "intro": stack.clear(); stack.push(introPage); return true
        case "home": toHome(); return true
        case "newgame": toHome(); stack.push(newGamePage); return true
        case "load": toHome(); stack.push(loadPage); return true
        case "settings": toHome(); stack.push(settingsPage); return true
        case "lobby": toHome(); stack.push(lobbyPage); return true
        case "codex": toHome(); stack.push(codexPage); return true
        case "gallery": toHome(); stack.push(galleryPage); return true
        case "campaign":
            if (!GameController.active)
                GameController.newGame(GameController.randomCountryKey(), "gdp", "normal", "7", [])
            startCampaign()
            return true
        case "campaign-fr":
            if (!GameController.active) GameController.newGame("fr", "gdp", "normal", "7", [])
            startCampaign()
            return true
        case "battle": {
            // a scripted assault from the French capital on its first defended neighbour
            if (!GameController.active) GameController.newGame("fr", "gdp", "normal", "7", [])
            startCampaign()
            const from = GameController.capitalOf("fr")
            GameController.recruit(from, "fighter", 2)
            const targets = GameController.attackTargets(from)
            let target = null
            for (const t of targets) if (!t.undefended) { target = t; break }
            if (!target) return false
            const ids = GameController.unitsOf(from).map(u => u.id)
            if (GameController.attack(from, target.id, ids) !== "needs-battle") return false
            GameController.acceptBattle(false)
            return true
        }
        }
        return false
    }

    Component { id: introPage; Intro { onDone: root.toHome() } }
    Component {
        id: homePage
        Home {
            onNewGame: root.open(newGamePage)
            onLoadGame: root.open(loadPage)
            onOnline: root.open(lobbyPage)
            onCodex: root.open(codexPage)
            onSettings: root.open(settingsPage)
            onGallery: root.open(galleryPage)
            onQuit: Qt.quit()
        }
    }
    Component {
        id: newGamePage
        NewGame {
            onBack: root.back()
            onStarted: root.startCampaign()
        }
    }
    Component {
        id: loadPage
        LoadGame {
            onBack: root.back()
            onLoaded: root.startCampaign()
        }
    }
    Component { id: settingsPage; SettingsView { onBack: root.back() } }
    Component { id: lobbyPage; Lobby { onBack: root.back() } }
    Component {
        id: campaignPage
        Campaign {
            onMenu: root.leaveCampaign()
            onOpenSettings: root.open(settingsPage)
        }
    }
    Component {
        id: battlePage
        Battle {
            onFinished: { root.back(); Audio.music("map") }
        }
    }
    Connections {
        target: GameController
        function onBattleRequested() {
            stack.push(battlePage)
            Audio.music("battle")
        }
        // the server started (or rebuilt, after a reconnect) the shared campaign
        function onOnlineCampaignStarted(resumed) {
            root.startCampaign()
            if (resumed) GameController.notice("Reconnected: the campaign was rebuilt from the match log", "good")
        }
    }
    Component { id: codexPage; Codex { onBack: root.back() } }
    Component { id: galleryPage; Gallery { onBack: root.back() } }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: introPage
        focus: true

        pushEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 1.02; to: 1; duration: 220; easing.type: Easing.OutCubic }
        }
        pushExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 160 }
        }
        popEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220; easing.type: Easing.OutCubic }
        }
        popExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 160 }
            NumberAnimation { property: "scale"; from: 1; to: 1.02; duration: 160 }
        }
        replaceEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 260 }
        }
        replaceExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 200 }
        }
    }

    ToastStrip {
        id: toasts
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        z: 100
    }

    Connections {
        target: GameController
        function onNotice(text, kind) { toasts.show(text, kind) }
    }

    Shortcut {
        sequence: "F11"
        onActivated: root.visibility = root.visibility === Window.FullScreen
                                       ? Window.Windowed : Window.FullScreen
    }

    // A missing asset is a build problem, not a runtime one - say so loudly.
    Text {
        visible: !GameController.worldLoaded
        anchors.centerIn: parent
        z: 200
        width: parent.width - 80
        text: "World data failed to load: " + GameController.loadError
        color: Style.danger
        font.pixelSize: Style.fontTitle
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }
}
