pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import AD
import AD.Theme
import AD.Components

// Settings: the theme, the sound, the effects and the game options. Every
// change applies live and is remembered.
FocusScope {
    id: page

    signal back()

    focus: true
    Keys.onEscapePressed: page.back()

    Settings {
        id: gameSettings
        category: "game"
        property bool autoResolve: false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            spacing: 16
            BackButton { onClicked: page.back() }
            PageTitle { text: "Settings"; subtitle: "Applied at once and remembered" }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: content.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            RowLayout {
                id: content
                width: parent.width
                spacing: 16

                Panel {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: "Theme"
                    padding: 16
                    implicitHeight: themeColumn.height + 16 + 30 + 16

                    Column {
                        id: themeColumn
                        width: parent.width
                        spacing: 10

                        Repeater {
                            model: Style.themes
                            Rectangle {
                                id: tile
                                required property var modelData
                                readonly property bool current: Style.theme === modelData.key
                                width: themeColumn.width
                                height: 64
                                radius: Style.radius
                                color: current ? Qt.alpha(Style.brass, 0.18) : tileHover.hovered ? Style.slateLight : Style.slate
                                border.width: current ? 2 : 1
                                border.color: current ? Style.brassBright : Style.brassDark
                                HoverHandler { id: tileHover }
                                TapHandler { onTapped: Style.theme = tile.modelData.key }

                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 14
                                    spacing: 14
                                    Row {
                                        spacing: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        // the four roles of this theme, previewed
                                        Repeater {
                                            model: Style.themeSwatches(tile.modelData.key)
                                            Rectangle {
                                                required property color modelData
                                                width: 22; height: 36; radius: 3
                                                color: modelData
                                                border.width: 1
                                                border.color: Qt.alpha(Style.paper, 0.4)
                                            }
                                        }
                                    }
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        Text {
                                            text: tile.modelData.name
                                            font.family: Style.displayFamily
                                            font.pixelSize: Style.fontBody + 3
                                            font.bold: true
                                            color: Style.onSlate
                                        }
                                        Text {
                                            text: tile.current ? "In use" : "Click to switch"
                                            font.pixelSize: Style.fontSmall
                                            color: Style.onSlateFaint
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 16

                    Panel {
                        Layout.fillWidth: true
                        title: "Sound"
                        padding: 16
                        implicitHeight: soundColumn.height + 16 + 30 + 16
                        Column {
                            id: soundColumn
                            width: parent.width
                            spacing: 2
                            OptionRow {
                                width: parent.width
                                label: "Master volume"
                                Slider { width: 180; from: 0; to: 1; value: Audio.masterVolume; onMoved: Audio.masterVolume = value }
                            }
                            OptionRow {
                                width: parent.width
                                label: "Effects"
                                Slider { width: 180; from: 0; to: 1; value: Audio.effectsVolume; onMoved: Audio.effectsVolume = value }
                            }
                            OptionRow {
                                width: parent.width
                                label: "Music"
                                Slider { width: 180; from: 0; to: 1; value: Audio.musicVolume; onMoved: Audio.musicVolume = value }
                            }
                            OptionRow {
                                width: parent.width
                                label: "Mute everything"
                                Switch { checked: Audio.muted; onToggled: Audio.muted = checked }
                            }
                        }
                    }

                    Panel {
                        Layout.fillWidth: true
                        title: "Effects"
                        padding: 16
                        implicitHeight: fxColumn.height + 16 + 30 + 16
                        Column {
                            id: fxColumn
                            width: parent.width
                            spacing: 2
                            OptionRow {
                                width: parent.width
                                label: "Paper grain"
                                hint: "Procedural grain on paper surfaces"
                                Switch { checked: Style.paperGrain; onToggled: Style.paperGrain = checked }
                            }
                            OptionRow {
                                width: parent.width
                                label: "Ocean swell"
                                hint: "Animated water under the chart; off is flat colour"
                                Switch { checked: Style.oceanWaves; onToggled: Style.oceanWaves = checked }
                            }
                            OptionRow {
                                width: parent.width
                                label: "Combat shake"
                                hint: "The board shakes when a general falls"
                                Switch { checked: Style.combatShake; onToggled: Style.combatShake = checked }
                            }
                            OptionRow {
                                width: parent.width
                                label: "City labels"
                                Switch { checked: Style.showLabels; onToggled: Style.showLabels = checked }
                            }
                            OptionRow {
                                width: parent.width
                                label: "Animations"
                                hint: "Transitions, glass blur and the drifting home chart"
                                Switch { checked: Style.animations; onToggled: Style.animations = checked }
                            }
                        }
                    }

                    Panel {
                        Layout.fillWidth: true
                        title: "Game"
                        padding: 16
                        implicitHeight: gameColumn.height + 16 + 30 + 16
                        Column {
                            id: gameColumn
                            width: parent.width
                            spacing: 2
                            OptionRow {
                                width: parent.width
                                label: "Auto-resolve my battles"
                                hint: "New campaigns skip the board: the engine plays both sides"
                                Switch { checked: gameSettings.autoResolve; onToggled: gameSettings.autoResolve = checked }
                            }
                            Text {
                                width: parent.width
                                topPadding: 8
                                text: "Keyboard controls are listed in the Codex."
                                font.pixelSize: Style.fontSmall
                                color: Style.onSlateFaint
                            }
                        }
                    }
                }
            }
        }
    }
}
