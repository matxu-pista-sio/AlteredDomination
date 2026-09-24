pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Layouts
import AD
import AD.Theme
import AD.Components

// Online play (docs/PROTOCOL.md): connect to an ad-server, see your record
// and the ladder, queue for a ranked opponent, agree the terms and pick
// your banner; the server then starts the same campaign on both machines
// and GameController takes it from there (onlineCampaignStarted).
FocusScope {
    id: page

    signal back()

    readonly property var client: LobbyClient
    readonly property bool online: client.connectionState === LobbyClient.Online
    readonly property bool paired: client.setupStage !== LobbyClient.NoSetup
    property string mode: "gdp"
    property string difficulty: "normal"

    focus: true
    Keys.onEscapePressed: page.back()
    Component.onCompleted: {
        if (!page.online) client.connectToServer()
        else { client.refreshStats(); client.refreshLeaderboard() }
    }

    function connectionLabel() {
        switch (client.connectionState) {
        case LobbyClient.Connecting: return "Connecting…"
        case LobbyClient.Authenticating: return "Signing in…"
        case LobbyClient.Online: return "Online as " + client.playerName
        default: return "Offline"
        }
    }
    function countdown() {
        const s = client.setupSecondsLeft
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }
    function modeName(k) { return k === "equality" ? "Equality" : "GDP" }
    function difficultyName(k) { return k === "easy" ? "Easy" : k === "hard" ? "Hard" : "Normal" }
    function cancelReason(reason) {
        switch (reason) {
        case "ready_timeout": return "Match cancelled: the setup clock ran out"
        case "world_mismatch": return "Match cancelled: the two games carry different world data"
        case "peer_disconnected": return "Match cancelled: the opponent left"
        case "server_restart": return "Your last match was lost when the server restarted"
        default: return "Match cancelled (" + reason + ")"
        }
    }

    Connections {
        target: page.client
        function onMatchCancelled(reason) { GameController.notice(page.cancelReason(reason), "bad") }
        function onLastErrorChanged() { if (page.client.lastError !== "") GameController.notice(page.client.lastError, "bad") }
        function onMatchFound() { Audio.play("hoverhome") }
        function onSessionEnded(reason) {
            GameController.notice("Signed in from another window or device - this one stands down", "bad")
        }
        function onConnectionStateChanged() {
            if (page.online) { page.client.refreshStats(); page.client.refreshLeaderboard() }
        }
    }

    // the ladder refreshes while the page is up
    Timer {
        interval: 15000
        repeat: true
        running: page.online && !page.paired
        onTriggered: { page.client.refreshLeaderboard(); page.client.refreshStats() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // -- the left column: who you are, the queue, the setup ------------------
        ColumnLayout {
            Layout.preferredWidth: 440
            Layout.maximumWidth: 440
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 12

            RowLayout {
                spacing: 16
                BackButton { onClicked: page.back() }
                PageTitle { text: "Online"; subtitle: "Ranked campaigns against another commander" }
            }

            Panel {
                Layout.fillWidth: true
                title: "Connection"
                padding: 14
                implicitHeight: connColumn.height + 14 + 30 + 14

                Column {
                    id: connColumn
                    width: parent.width
                    spacing: 8
                    Row {
                        spacing: 8
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 10; height: 10; radius: 5
                            color: page.online ? Style.lamp : page.client.connectionState === LobbyClient.Disconnected ? Style.danger : Style.brass
                        }
                        Text {
                            text: page.connectionLabel()
                            font.family: Style.displayFamily
                            font.pixelSize: Style.fontBody + 2
                            font.bold: true
                            color: Style.onSlate
                        }
                    }
                    Row {
                        spacing: 8
                        TextField {
                            id: nameField
                            width: connColumn.width - 96
                            text: page.client.playerName
                            placeholderText: "Your name"
                            enabled: !page.paired
                            onEditingFinished: page.client.playerName = text
                        }
                        Button {
                            width: 88
                            text: page.online ? "Reconnect" : "Connect"
                            enabled: !page.paired && page.client.connectionState !== LobbyClient.Connecting
                            onClicked: { page.client.playerName = nameField.text; page.client.serverUrl = urlField.text
                                         page.client.disconnectFromServer(); page.client.connectToServer() }
                        }
                    }
                    TextField {
                        id: urlField
                        width: connColumn.width
                        text: page.client.serverUrl
                        placeholderText: "ws://host:9977"
                        enabled: !page.paired
                        font.pixelSize: Style.fontSmall + 1
                        onEditingFinished: page.client.serverUrl = text
                    }
                    Text {
                        text: "A name change takes effect on the next connection."
                        font.pixelSize: Style.fontSmall
                        color: Style.onSlateFaint
                    }
                }
            }

            Panel {
                Layout.fillWidth: true
                title: "Your record"
                padding: 14
                implicitHeight: recordRow.height + 14 + 30 + 14
                Row {
                    id: recordRow
                    spacing: 22
                    Repeater {
                        model: [["ELO", page.client.selfStats.elo], ["Wins", page.client.selfStats.wins],
                                ["Losses", page.client.selfStats.losses], ["Draws", page.client.selfStats.draws],
                                ["Rank", page.client.leaderboardSelfRank > 0 ? "#" + page.client.leaderboardSelfRank : "—"]]
                        Column {
                            id: stat
                            required property var modelData
                            spacing: 1
                            Text {
                                text: stat.modelData[0]
                                font.pixelSize: Style.fontSmall - 1
                                font.letterSpacing: 1.2
                                font.capitalization: Font.AllUppercase
                                color: Style.onSlateFaint
                            }
                            Text {
                                text: stat.modelData[1] === undefined ? "—" : "" + stat.modelData[1]
                                font.family: Style.displayFamily
                                font.pixelSize: Style.fontTitle
                                font.bold: true
                                font.features: { "tnum": 1 }
                                color: Style.brassBright
                            }
                        }
                    }
                }
            }

            // -- the queue / the setup --------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: page.paired ? "Match found" : "Ranked queue"
                padding: 14

                // waiting for an opponent
                Column {
                    visible: !page.paired
                    width: parent.width
                    spacing: 10
                    Text {
                        width: parent.width
                        text: page.client.queueState === LobbyClient.Queued
                              ? "Searching… the queue pairs neighbours on the ladder every few seconds."
                              : "One ranked queue: you are paired with the nearest rating waiting. The higher-rated player opens every round."
                        font.pixelSize: Style.fontBody
                        color: Style.onSlate
                        wrapMode: Text.Wrap
                    }
                    Row {
                        spacing: 10
                        Button {
                            id: queueButton
                            width: 200
                            height: 44
                            text: page.client.queueState === LobbyClient.Queued ? "Leave the queue" : "Find an opponent"
                            primary: page.client.queueState !== LobbyClient.Queued
                            enabled: page.online
                            onClicked: page.client.queueState === LobbyClient.Queued ? page.client.leaveQueue() : page.client.joinQueue()
                        }
                        Item {
                            id: queueSpinner
                            width: 24; height: 24
                            anchors.verticalCenter: parent.verticalCenter
                            visible: page.client.queueState === LobbyClient.Queued
                            Rectangle { anchors.fill: parent; radius: 12; color: "transparent"; border.width: 2; border.color: Style.lamp; opacity: 0.35 }
                            Rectangle { width: 8; height: 8; radius: 4; x: 8; y: -2; color: Style.lamp }
                            RotationAnimation on rotation { from: 0; to: 360; duration: 1100; loops: Animation.Infinite; running: Style.animations && queueSpinner.visible }
                        }
                    }
                    Text {
                        visible: page.client.lastResult.reason !== undefined
                        width: parent.width
                        text: page.client.lastResult.disputed ? "Last match disputed - no rating change"
                            : "Last match: " + (page.client.lastResult.eloDelta >= 0 ? "+" : "") + page.client.lastResult.eloDelta + " ELO"
                        font.pixelSize: Style.fontSmall + 1
                        color: Style.onSlateFaint
                    }
                }

                // paired: the terms, then the banners
                Column {
                    visible: page.paired
                    width: parent.width
                    spacing: 10
                    Row {
                        spacing: 10
                        Text {
                            text: "vs  " + (page.client.opponent.name || "") + "  ·  " + (page.client.opponent.elo || 1000) + " ELO"
                            font.family: Style.displayFamily
                            font.pixelSize: Style.fontBody + 3
                            font.bold: true
                            color: Style.onSlate
                        }
                        Text {
                            anchors.baseline: parent.children[0].baseline
                            text: page.countdown()
                            font.pixelSize: Style.fontBody
                            font.features: { "tnum": 1 }
                            color: page.client.setupSecondsLeft <= 10 ? Style.danger : Style.onSlateFaint
                        }
                    }
                    Text {
                        text: "You play " + (page.client.side === 0 ? "first" : "second") + " in every round."
                        font.pixelSize: Style.fontSmall + 1
                        color: Style.onSlateFaint
                    }
                    PipDivider { width: parent.width }

                    // stage 1: the terms
                    Column {
                        visible: page.client.setupStage === LobbyClient.Terms
                        width: parent.width
                        spacing: 8
                        Text {
                            width: parent.width
                            text: page.client.setupChooser ? "You set the terms of this campaign."
                                                           : "Waiting for " + (page.client.opponent.name || "the opponent") + " to set the terms…"
                            font.pixelSize: Style.fontBody
                            color: Style.onSlate
                            wrapMode: Text.Wrap
                        }
                        SectionHeader { visible: page.client.setupChooser; width: parent.width; text: "Mode" }
                        Row {
                            visible: page.client.setupChooser
                            spacing: 8
                            Button { text: "GDP"; checkable: true; checked: page.mode === "gdp"; onClicked: page.mode = "gdp" }
                            Button { text: "Equality"; checkable: true; checked: page.mode === "equality"; onClicked: page.mode = "equality" }
                        }
                        SectionHeader { visible: page.client.setupChooser; width: parent.width; text: "Difficulty of the AI powers" }
                        Row {
                            visible: page.client.setupChooser
                            spacing: 8
                            Repeater {
                                model: [["easy", "Easy"], ["normal", "Normal"], ["hard", "Hard"]]
                                Button {
                                    required property var modelData
                                    text: modelData[1]
                                    checkable: true
                                    checked: page.difficulty === modelData[0]
                                    onClicked: page.difficulty = modelData[0]
                                }
                            }
                        }
                        Button {
                            visible: page.client.setupChooser
                            width: parent.width
                            height: 42
                            text: "Propose these terms"
                            primary: true
                            onClicked: page.client.setTerms(page.mode, page.difficulty)
                        }
                    }

                    // stage 2: the banners
                    Column {
                        visible: page.client.setupStage === LobbyClient.Countries
                        width: parent.width
                        spacing: 8
                        Text {
                            width: parent.width
                            text: page.modeName(page.client.setupTerms.mode) + " mode  ·  " + page.difficultyName(page.client.setupTerms.difficulty) + " AI"
                            font.pixelSize: Style.fontBody
                            color: Style.brassBright
                        }
                        Text {
                            width: parent.width
                            text: page.client.myCountry !== ""
                                  ? "Your banner is set. " + (page.client.opponentCountry !== "" ? "Starting…" : "Waiting for the opponent's…")
                                  : "Pick your banner on the right. The opponent" + (page.client.opponentCountry !== "" ? " holds " + (GameController.countryInfo(page.client.opponentCountry).name || page.client.opponentCountry) + "." : " is choosing too.")
                            font.pixelSize: Style.fontBody
                            color: Style.onSlate
                            wrapMode: Text.Wrap
                        }
                        Row {
                            spacing: 12
                            Flag { width: 64; height: 48; source: countryPicker.selected ? countryPicker.selected.flag : "" }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    text: countryPicker.selected ? countryPicker.selected.name : "—"
                                    font.family: Style.displayFamily
                                    font.pixelSize: Style.fontBody + 3
                                    font.bold: true
                                    color: Style.onSlate
                                }
                                Text {
                                    text: countryPicker.selected ? countryPicker.selected.income + " per round  ·  " + countryPicker.selected.cities + " cities" : ""
                                    font.pixelSize: Style.fontSmall
                                    color: Style.onSlateFaint
                                }
                            }
                        }
                        Button {
                            width: parent.width
                            height: 42
                            text: page.client.myCountry === countryPicker.selectedKey && page.client.myCountry !== "" ? "Banner sent" : "Raise this banner"
                            primary: true
                            enabled: countryPicker.selected !== null && countryPicker.selectedKey !== page.client.opponentCountry
                                     && page.client.myCountry !== countryPicker.selectedKey
                            onClicked: page.client.pickCountry(countryPicker.selectedKey)
                        }
                    }
                }
            }
        }

        // -- the right column: the ladder, or the banner grid during setup ------------
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: page.client.setupStage === LobbyClient.Countries ? "Your banner" : "The ladder"
            padding: 12

            CountryPicker {
                id: countryPicker
                anchors.fill: parent
                visible: page.client.setupStage === LobbyClient.Countries
                selectedKey: GameController.humanKey !== "" ? GameController.humanKey : "fr"
                disabledKeys: page.client.opponentCountry !== "" ? [page.client.opponentCountry] : []
                onAccepted: if (selected && selectedKey !== page.client.opponentCountry) page.client.pickCountry(selectedKey)
                onVisibleChanged: if (visible) focusSearch()
            }

            ColumnLayout {
                anchors.fill: parent
                visible: page.client.setupStage !== LobbyClient.Countries
                spacing: 8
                Row {
                    spacing: 10
                    Text {
                        text: page.client.leaderboardTotal + (page.client.leaderboardTotal === 1 ? " commander" : " commanders")
                        font.pixelSize: Style.fontSmall + 1
                        color: Style.onSlateFaint
                    }
                    Button {
                        id: onlineOnly
                        text: "Online only"
                        checkable: true
                        height: 26
                        onToggled: page.client.refreshLeaderboard(50, checked)
                    }
                }
                ListView {
                    id: ladder
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: page.client.leaderboard
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        width: ladder.width
                        height: 34
                        radius: Style.radius
                        color: modelData.me ? Qt.alpha(Style.brass, 0.18) : "transparent"
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 10
                            spacing: 12
                            Text {
                                width: 36
                                text: "#" + row.modelData.rank
                                font.features: { "tnum": 1 }
                                font.pixelSize: Style.fontBody
                                color: Style.onSlateFaint
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8; height: 8; radius: 4
                                color: row.modelData.online ? Style.lamp : Style.brassDark
                            }
                            Text {
                                width: ladder.width - 260
                                text: row.modelData.name
                                elide: Text.ElideRight
                                font.pixelSize: Style.fontBody + (row.modelData.me ? 1 : 0)
                                font.bold: row.modelData.me
                                color: Style.onSlate
                            }
                            Text {
                                width: 56
                                text: row.modelData.elo
                                font.family: Style.displayFamily
                                font.bold: true
                                font.features: { "tnum": 1 }
                                font.pixelSize: Style.fontBody + 1
                                color: Style.brassBright
                            }
                            Text {
                                text: row.modelData.wins + " W  " + row.modelData.losses + " L  " + row.modelData.draws + " D"
                                font.features: { "tnum": 1 }
                                font.pixelSize: Style.fontSmall
                                color: Style.onSlateFaint
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: ladder.count === 0
                        text: page.online ? "Nobody on the ladder yet" : "Connect to see the ladder"
                        color: Style.onSlateFaint
                    }
                }
            }
        }
    }
}
