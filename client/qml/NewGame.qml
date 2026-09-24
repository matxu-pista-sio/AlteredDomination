pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Layouts
import QtCore
import AD
import AD.Theme
import AD.Components

// New game: pick a country from the searchable grid, set the mode, the
// difficulty, the seed and a second seat, and start.
FocusScope {
    id: page

    signal back()
    signal started()

    property string selectedKey: gameSettings.lastCountry
    property var countries: GameController.allCountries()
    property string continent: "All"
    readonly property var filtered: {
        const needle = search.text.trim().toLowerCase()
        return countries.filter(c => (continent === "All" || c.continent === continent)
                                  && (needle === "" || c.name.toLowerCase().indexOf(needle) >= 0
                                      || c.capitalName.toLowerCase().indexOf(needle) >= 0))
    }
    readonly property var selected: {
        for (const c of countries) if (c.key === selectedKey) return c
        return null
    }

    Settings {
        id: gameSettings
        category: "game"
        property string lastCountry: "fr"
        property string mode: "gdp"
        property string difficulty: "normal"
    }

    focus: true
    Keys.onEscapePressed: page.back()
    Component.onCompleted: search.forceActiveFocus()

    function fmtBig(n) {
        if (n >= 1e12) return (n / 1e12).toFixed(2) + " T"
        if (n >= 1e9) return (n / 1e9).toFixed(1) + " B"
        if (n >= 1e6) return (n / 1e6).toFixed(1) + " M"
        if (n >= 1e3) return (n / 1e3).toFixed(0) + " k"
        return "" + n
    }
    function start() {
        if (!page.selected) return
        const extra = hotseat.checked ? [countries[secondSeat.currentIndex].key] : []
        if (GameController.newGame(page.selectedKey, gameSettings.mode, gameSettings.difficulty,
                                   seed.text, extra)) {
            Audio.play("click")
            page.started()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            RowLayout {
                spacing: 16
                BackButton { onClicked: page.back() }
                PageTitle { text: "New game"; subtitle: "Choose the banner you will raise over the world" }
            }

            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 12

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        TextField {
                            id: search
                            Layout.fillWidth: true
                            focus: true
                            placeholderText: "Search a country or a capital…"
                            Keys.onReturnPressed: if (page.filtered.length > 0) page.selectedKey = page.filtered[0].key
                        }
                        Button {
                            text: "Random"
                            ToolTip.visible: hovered
                            ToolTip.text: "Let fate pick"
                            onClicked: page.selectedKey = GameController.randomCountryKey()
                        }
                    }

                    TabBar {
                        id: continents
                        Layout.fillWidth: true
                        background: Item {}
                        Repeater {
                            model: ["All", "Europe", "Asia", "Africa", "North America", "South America", "Oceania"]
                            TabButton {
                                required property string modelData
                                text: modelData
                                width: implicitWidth
                                onClicked: page.continent = modelData
                            }
                        }
                    }

                    GridView {
                        id: grid
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 176)))
                        cellHeight: 112
                        model: page.filtered
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Item {
                            id: card
                            required property var modelData
                            width: grid.cellWidth
                            height: grid.cellHeight
                            readonly property bool current: modelData.key === page.selectedKey

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 4
                                radius: Style.radius
                                color: card.current ? Qt.alpha(Style.brass, 0.2)
                                     : cardHover.hovered ? Style.slateLight : Style.slate
                                border.width: card.current ? 2 : 1
                                border.color: card.current ? Style.brassBright
                                            : cardHover.hovered ? Style.brass : Style.brassDark
                                Behavior on color { ColorAnimation { duration: 90 } }

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Flag {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 60
                                        height: 45
                                        source: card.modelData.flag
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: card.width - 22
                                        text: card.modelData.name
                                        font.family: Style.displayFamily
                                        font.pixelSize: Style.fontBody + 1
                                        font.bold: true
                                        color: Style.onSlate
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: card.modelData.income + " / round  ·  " + card.modelData.cities
                                              + (card.modelData.cities === 1 ? " city" : " cities")
                                        font.pixelSize: Style.fontSmall
                                        color: Style.onSlateFaint
                                    }
                                }
                                HoverHandler { id: cardHover }
                                TapHandler {
                                    onTapped: page.selectedKey = card.modelData.key
                                    onDoubleTapped: { page.selectedKey = card.modelData.key; page.start() }
                                }
                            }
                        }
                    }

                    Text {
                        visible: page.filtered.length === 0
                        Layout.alignment: Qt.AlignHCenter
                        text: "No country matches"
                        color: Style.onSlateFaint
                    }
                }
            }
        }

        Panel {
            id: sheet
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            title: "Your banner"
            padding: 16

            Flickable {
                anchors.fill: parent
                contentHeight: sheetColumn.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                Column {
                    id: sheetColumn
                    width: parent.width
                    spacing: 12

                    Row {
                        spacing: 14
                        Flag {
                            width: 96
                            height: 72
                            source: page.selected ? page.selected.flag : ""
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                width: sheetColumn.width - 110
                                text: page.selected ? page.selected.name : "—"
                                font.family: Style.displayFamily
                                font.pixelSize: Style.fontTitle + 2
                                font.bold: true
                                color: Style.onSlate
                                wrapMode: Text.Wrap
                            }
                            Text {
                                text: page.selected ? page.selected.continent + "  ·  " + page.selected.capitalName : ""
                                font.pixelSize: Style.fontSmall + 1
                                color: Style.onSlateFaint
                            }
                        }
                    }

                    Grid {
                        columns: 2
                        columnSpacing: 20
                        rowSpacing: 4
                        Text { text: "GDP"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall + 1 }
                        Text {
                            text: page.selected ? "$ " + page.fmtBig(page.selected.gdp)
                                                  + (page.selected.gdpYear ? "  (" + page.selected.gdpYear + ")" : "") : ""
                            color: Style.onSlate; font.pixelSize: Style.fontSmall + 1
                        }
                        Text { text: "Population"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall + 1 }
                        Text { text: page.selected ? page.fmtBig(page.selected.population) : ""; color: Style.onSlate; font.pixelSize: Style.fontSmall + 1 }
                        Text { text: "Cities"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall + 1 }
                        Text { text: page.selected ? "" + page.selected.cities : ""; color: Style.onSlate; font.pixelSize: Style.fontSmall + 1 }
                        Text { text: "Income (GDP mode)"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall + 1 }
                        Text { text: page.selected ? page.selected.income + " per round" : ""; color: Style.brassBright; font.pixelSize: Style.fontSmall + 1; font.bold: true }
                    }

                    PipDivider { width: sheetColumn.width }

                    SectionHeader { width: sheetColumn.width; text: "Mode" }
                    Row {
                        spacing: 8
                        Button {
                            text: "GDP"
                            checkable: true
                            checked: gameSettings.mode === "gdp"
                            onClicked: gameSettings.mode = "gdp"
                            ToolTip.visible: hovered
                            ToolTip.text: "Income follows the real economy: rich countries start strong"
                        }
                        Button {
                            text: "Equality"
                            checkable: true
                            checked: gameSettings.mode === "equality"
                            onClicked: gameSettings.mode = "equality"
                            ToolTip.visible: hovered
                            ToolTip.text: "Every country earns the same: only the map matters"
                        }
                    }

                    SectionHeader { width: sheetColumn.width; text: "Difficulty" }
                    Row {
                        spacing: 8
                        Repeater {
                            model: [["easy", "Easy", "AI income −20 %, shallow battle search"],
                                    ["normal", "Normal", "Even terms"],
                                    ["hard", "Hard", "AI income +25 %, deep battle search"]]
                            Button {
                                required property var modelData
                                text: modelData[1]
                                checkable: true
                                checked: gameSettings.difficulty === modelData[0]
                                onClicked: gameSettings.difficulty = modelData[0]
                                ToolTip.visible: hovered
                                ToolTip.text: modelData[2]
                            }
                        }
                    }

                    SectionHeader { width: sheetColumn.width; text: "Seed" }
                    Row {
                        spacing: 8
                        TextField {
                            id: seed
                            width: sheetColumn.width - 72
                            placeholderText: "Leave empty for a random world order"
                        }
                        Button {
                            text: "Roll"
                            ToolTip.visible: hovered
                            ToolTip.text: "Roll a seed"
                            onClicked: seed.text = "" + Math.floor(Math.random() * 1e9)
                        }
                    }

                    SectionHeader { width: sheetColumn.width; text: "Hotseat" }
                    Switch {
                        id: hotseat
                        text: "A second player at this table"
                    }
                    ComboBox {
                        id: secondSeat
                        visible: hotseat.checked
                        width: sheetColumn.width
                        model: page.countries
                        textRole: "name"
                        currentIndex: 0
                    }

                    Item { width: 1; height: 8 }

                    Button {
                        id: startButton
                        width: sheetColumn.width
                        height: 46
                        text: "Start the campaign"
                        primary: true
                        enabled: page.selected !== null
                                 && (!hotseat.checked || page.countries[secondSeat.currentIndex].key !== page.selectedKey)
                        onClicked: page.start()
                    }
                }
            }
        }
    }
}
