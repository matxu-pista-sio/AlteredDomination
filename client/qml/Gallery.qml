pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Layouts
import AD
import AD.Theme
import AD.Components

// Every control and component in every theme (issue #12's proof page).
FocusScope {
    id: page

    signal back()

    focus: true
    Keys.onEscapePressed: page.back()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            spacing: 16
            BackButton { onClicked: page.back() }
            PageTitle { text: "Gallery"; subtitle: "The ADDesktop controls and the shared components" }
            Item { Layout.fillWidth: true }
            ComboBox {
                model: Style.themes
                textRole: "name"
                valueRole: "key"
                currentIndex: { for (let i = 0; i < Style.themes.length; ++i) if (Style.themes[i].key === Style.theme) return i; return 0 }
                onActivated: Style.theme = currentValue
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: flow.height
            clip: true
            ScrollBar.vertical: ScrollBar {}

            Flow {
                id: flow
                width: parent.width
                spacing: 16

                Panel {
                    title: "Buttons"
                    width: 420; height: 150
                    Flow {
                        anchors.fill: parent
                        spacing: 8
                        Button { text: "Button" }
                        Button { text: "Highlighted"; highlighted: true }
                        Button { text: "Checked"; checkable: true; checked: true }
                        Button { text: "Primary"; primary: true }
                        Button { text: "Danger"; danger: true }
                        Button { text: "Disabled"; enabled: false }
                        Button { text: "Disabled lit"; highlighted: true; enabled: false }
                        IconButton { glyph: "⚙"; tip: "Settings" }
                        BackButton {}
                    }
                }

                Panel {
                    title: "Fields"
                    width: 420; height: 190
                    Column {
                        anchors.fill: parent
                        spacing: 8
                        TextField { width: 260; placeholderText: "A writing field" }
                        TextField { width: 260; text: "Disabled"; enabled: false }
                        ComboBox { width: 260; model: ["Situation room", "Paper atlas", "Night ops"] }
                        SpinBox { from: 0; to: 99; value: 12 }
                    }
                }

                Panel {
                    title: "Toggles"
                    width: 300; height: 230
                    Column {
                        anchors.fill: parent
                        spacing: 6
                        CheckBox { text: "Checked"; checked: true }
                        CheckBox { text: "Unchecked" }
                        Switch { text: "On"; checked: true }
                        Switch { text: "Off" }
                        Slider { width: 240; value: 0.6 }
                    }
                }

                Panel {
                    title: "Tabs and text"
                    width: 420; height: 190
                    Column {
                        anchors.fill: parent
                        spacing: 8
                        TabBar {
                            background: Item {}
                            TabButton { text: "Rules"; width: implicitWidth }
                            TabButton { text: "Units"; width: implicitWidth }
                            TabButton { text: "Controls"; width: implicitWidth }
                        }
                        Label { text: "Body text in Source Sans 3 on slate." }
                        Text { text: "Display text in Rajdhani"; font.family: Style.displayFamily; font.pixelSize: Style.fontTitle; font.bold: true; color: Style.brassBright }
                        SectionHeader { width: 380; text: "Section header" }
                        PipDivider { width: 380 }
                    }
                }

                Panel {
                    title: "Cards and gauges"
                    width: 420; height: 200
                    Row {
                        anchors.fill: parent
                        spacing: 12
                        UnitTile {
                            name: "Tank"; cost: 50; classText: "Machine · Land"
                            icon: "qrc:/assets/units/icons/tank.svg"
                            description: "A tile with a tooltip"
                        }
                        UnitTile {
                            name: "Fighter"; cost: 120; classText: "Machine · Air"
                            icon: "qrc:/assets/units/icons/fighter.svg"
                            selected: true
                        }
                        Column {
                            spacing: 10
                            anchors.verticalCenter: parent.verticalCenter
                            Gauge { width: 130; value: 42; threshold: 60 }
                            Gauge { width: 130; value: 75; threshold: 60; fillColor: Style.brass }
                            Row { spacing: 6; KeyHint { key: "E" } KeyHint { key: "Esc" } KeyHint { key: "Ctrl+S" } }
                            Flag { source: "qrc:/assets/flags/fr.svg" }
                        }
                    }
                }

                Panel {
                    title: "Toasts"
                    width: 360; height: 200
                    Column {
                        anchors.fill: parent
                        spacing: 6
                        Toast { text: "Paris captured"; kind: "good" }
                        Toast { text: "Lyon fell to Germany"; kind: "bad" }
                        Toast { text: "Japan took Beijing, the capital of China"; kind: "info" }
                    }
                }

                Panel {
                    title: "Pattern diagram"
                    width: 300; height: 330
                    PatternDiagram {
                        anchors.centerIn: parent
                        cellSize: 22
                        icon: "qrc:/assets/units/icons/artillery.svg"
                        moves: [{dx: 1, dy: 0, path: []}, {dx: -1, dy: 0, path: []}]
                        strikes: [{dx: 3, dy: 0, path: [{dx: 1, dy: 0}, {dx: 2, dy: 0}], affects: ["human"]},
                                  {dx: 4, dy: 1, path: [], affects: ["human"]}, {dx: 4, dy: -1, path: [], affects: ["human"]}]
                    }
                }

                Rectangle {
                    width: 420; height: 200
                    radius: Style.radius
                    color: Style.paper
                    border.width: 1
                    border.color: Style.brassDark
                    PaperOverlay { anchors.fill: parent; radius: Style.radius }
                    Column {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Text { text: "A paper sheet"; font.family: Style.displayFamily; font.pixelSize: Style.fontTitle; font.bold: true; color: Style.ink }
                        Text { width: parent.width; text: "Popups and cards are paper: ink text over procedural grain, brass rim. The grain is a shader and switches off in Settings."; color: Style.ink; wrapMode: Text.Wrap }
                        Button { text: "Open a dialog"; onClicked: dialog.open() }
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: dialog
        title: "Delete this save?"
        text: "The slot and its metadata will be removed. This cannot be undone."
        confirmText: "Delete"
        destructive: true
    }
}
