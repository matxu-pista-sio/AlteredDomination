pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AD
import AD.Theme
import AD.Components

// The codex: the rules in brief, every unit with its pattern drawn from
// the catalog, and the controls.
FocusScope {
    id: page

    signal back()

    focus: true
    Keys.onEscapePressed: page.back()

    property int unitIndex: 0
    readonly property var unit: GameController.catalog.count > 0
                                ? GameController.catalog.info(unitKeys[unitIndex]) : null
    readonly property var unitKeys: {
        const keys = []
        for (let i = 0; i < GameController.catalog.count; ++i)
            keys.push(GameController.catalog.data(GameController.catalog.index(i, 0), Qt.UserRole + 1))
        return keys
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            spacing: 16
            BackButton { onClicked: page.back() }
            PageTitle { text: "Codex"; subtitle: "How the world is won" }
            Item { Layout.fillWidth: true }
            TabBar {
                id: tabs
                background: Item {}
                TabButton { text: "Rules"; width: implicitWidth }
                TabButton { text: "Units"; width: implicitWidth }
                TabButton { text: "Controls"; width: implicitWidth }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // -- rules ------------------------------------------------------------
            Panel {
                padding: 20
                Flickable {
                    anchors.fill: parent
                    contentHeight: rules.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    Column {
                        id: rules
                        width: parent.width
                        spacing: 14
                        Repeater {
                            model: [
                                ["The campaign", "Every country on the chart is a player; you are one of them. The world is <b>" + GameController.cities.rowCount() + " cities</b> joined by land and sea links, and a round is one turn for every surviving country: yours first, then the rest of the world in one sweep."],
                                ["Income", "Each city yields income once per round. In <b>GDP mode</b> a country's cities share a budget shaped by its real economy, so the rich start strong; in <b>Equality mode</b> every country starts with the same budget. Capitals yield a quarter more. The AI's income is scaled by the difficulty."],
                                ["Recruit, move, attack", "Funds buy units in any city you own. A unit that moved or attacked this turn has acted and rests until the next. Units move along links between your own cities, and attack along links into foreign ones. An <b>undefended city is captured at once</b>; a defended one starts a board battle. Every city starts with a home guard."],
                                ["Capitals and elimination", "Taking a capital loots half its owner's treasury. A country with no cities left is eliminated at once, its remaining units with it."],
                                ["Victory", "At the end of a round, own cities worth <b>" + GameController.dominationPercent + " % of the world's income</b> - or every city - and the world is yours. Lose your last city and it is over."],
                                ["The board battle", "Attackers deploy on the left three columns, defenders on the right; the board is 14 cells long and as wide as the larger force needs. Each side then promotes <b>generals</b>: one, plus one more per eight units. Any unit can be crowned, but a general leads from a jeep - it becomes a soldier for good."],
                                ["Turns", "The attacker acts first. A side takes as many actions per turn as it has living generals: a move or a strike each. Every unit type has its own move and strike patterns - see the Units tab. A strike destroys the target if the striker's weapon can affect its class (human, machine, land, air) and leaves the striker in place."],
                                ["Ending a battle", "A side with no living generals loses at once. A side may surrender, or offer a draw that the other side accepts by offering too. The battle is drawn after 80 turns, or 30 turns without a strike. Winning attackers take the city; losing attackers retreat home with their survivors."],
                            ]
                            Column {
                                id: section
                                required property var modelData
                                width: rules.width
                                spacing: 4
                                Text {
                                    text: section.modelData[0]
                                    font.family: Style.displayFamily
                                    font.pixelSize: Style.fontTitle
                                    font.bold: true
                                    color: Style.brassBright
                                }
                                Text {
                                    width: rules.width
                                    text: section.modelData[1]
                                    textFormat: Text.RichText
                                    font.pixelSize: Style.fontBody + 1
                                    color: Style.onSlate
                                    wrapMode: Text.Wrap
                                    lineHeight: 1.25
                                }
                            }
                        }
                    }
                }
            }

            // -- units ------------------------------------------------------------
            RowLayout {
                spacing: 16
                Panel {
                    Layout.preferredWidth: 250
                    Layout.fillHeight: true
                    padding: 8
                    ListView {
                        anchors.fill: parent
                        clip: true
                        spacing: 4
                        model: GameController.catalog
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: UnitTile {
                            required property int index
                            required property var model
                            width: ListView.view.width
                            compact: true
                            typeKey: model.typeKey
                            name: model.name
                            cost: model.cost
                            icon: model.icon
                            classText: model.classText
                            selected: page.unitIndex === index
                            onClicked: page.unitIndex = index
                        }
                    }
                }
                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 24
                    visible: page.unit !== null

                    RowLayout {
                        anchors.fill: parent
                        spacing: 28

                        Column {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: 12
                            Row {
                                spacing: 16
                                Rectangle {
                                    width: 72; height: 72; radius: 36
                                    color: Style.brass
                                    border.width: 1
                                    border.color: Qt.alpha(Style.paper, 0.4)
                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        source: page.unit ? page.unit.icon : ""
                                        sourceSize: Qt.size(96, 96)
                                        fillMode: Image.PreserveAspectFit
                                    }
                                }
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    Text {
                                        text: page.unit ? page.unit.name : ""
                                        font.family: Style.displayFamily
                                        font.pixelSize: Style.fontDisplay - 4
                                        font.bold: true
                                        color: Style.onSlate
                                    }
                                    Text {
                                        text: page.unit ? "Cost " + page.unit.cost + "  ·  " + page.unit.classText : ""
                                        font.pixelSize: Style.fontBody + 1
                                        color: Style.brassBright
                                    }
                                }
                            }
                            Text {
                                width: parent.width
                                text: page.unit ? page.unit.description : ""
                                font.pixelSize: Style.fontBody + 1
                                color: Style.onSlate
                                wrapMode: Text.Wrap
                                lineHeight: 1.25
                            }
                            Text {
                                width: parent.width
                                text: page.unit
                                      ? "Moves: " + page.unit.moves.length + " patterns  ·  Strikes: " + page.unit.strikes.length
                                        + " patterns" + (page.unit.strikes.length > 0
                                                          ? " affecting " + page.unit.strikes[0].affects.join(", ") : "")
                                      : ""
                                font.pixelSize: Style.fontSmall + 1
                                color: Style.onSlateFaint
                                wrapMode: Text.Wrap
                            }
                        }

                        Column {
                            Layout.alignment: Qt.AlignTop
                            spacing: 10
                            PatternDiagram {
                                moves: page.unit ? page.unit.moves : []
                                strikes: page.unit ? page.unit.strikes : []
                                icon: page.unit ? page.unit.icon : ""
                                cellSize: 22
                            }
                            Row {
                                spacing: 14
                                Row { spacing: 5; Rectangle { width: 12; height: 12; y: 2; color: Qt.alpha(Style.moveTarget, 0.55) } Text { text: "move"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall } }
                                Row { spacing: 5; Rectangle { width: 12; height: 12; y: 2; color: Qt.alpha(Style.attackTarget, 0.6) } Text { text: "strike"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall } }
                                Row { spacing: 5; Rectangle { width: 12; height: 12; y: 2; color: Style.boardDark; Rectangle { anchors.centerIn: parent; width: 4; height: 4; radius: 2; color: Style.paper } } Text { text: "must be empty"; color: Style.onSlateFaint; font.pixelSize: Style.fontSmall } }
                            }
                            Text {
                                text: "The enemy is upward."
                                color: Style.onSlateFaint
                                font.pixelSize: Style.fontSmall
                            }
                        }
                    }
                }
            }

            // -- controls ----------------------------------------------------------
            Panel {
                padding: 20
                Flickable {
                    anchors.fill: parent
                    contentHeight: controls.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    Row {
                        id: controls
                        spacing: 40
                        Repeater {
                            model: [
                                ["The map", [["E", "End the turn"], ["F", "Find a city"], ["Tab", "Next city with units to spend"],
                                             ["R", "Recruit in the selected city"], ["M", "Move from the selected city"], ["A", "Attack from the selected city"],
                                             ["← ↑ → ↓", "Pan"], ["+ / −", "Zoom"], ["0", "Fit the world"], ["Home", "Fly to your capital"],
                                             ["Esc", "Close a sheet, then the menu"], ["Ctrl+S", "Quick save"]]],
                                ["The battle", [["Enter", "Ready, then end the turn"], ["D", "Offer a draw"], ["Esc", "Clear the selection, then quit"],
                                                ["Right click", "A unit's card"], ["F11", "Full screen"]]],
                            ]
                            Column {
                                id: group
                                required property var modelData
                                spacing: 8
                                Text {
                                    text: group.modelData[0]
                                    font.family: Style.displayFamily
                                    font.pixelSize: Style.fontTitle
                                    font.bold: true
                                    color: Style.brassBright
                                }
                                Repeater {
                                    model: group.modelData[1]
                                    Row {
                                        id: shortcut
                                        required property var modelData
                                        spacing: 12
                                        KeyHint { key: shortcut.modelData[0]; anchors.verticalCenter: parent.verticalCenter }
                                        Text { text: shortcut.modelData[1]; color: Style.onSlate; font.pixelSize: Style.fontBody; anchors.verticalCenter: parent.verticalCenter }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
