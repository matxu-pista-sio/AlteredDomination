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
    readonly property var countries: picker.countries
    readonly property var selected: picker.selected

    Settings {
        id: gameSettings
        category: "game"
        property string lastCountry: "fr"
        property string mode: "gdp"
        property string difficulty: "normal"
    }

    focus: true
    Keys.onEscapePressed: page.back()
    Component.onCompleted: picker.focusSearch()

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

                CountryPicker {
                    id: picker
                    anchors.fill: parent
                    selectedKey: page.selectedKey
                    onSelectedKeyChanged: page.selectedKey = selectedKey
                    onAccepted: page.start()
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
