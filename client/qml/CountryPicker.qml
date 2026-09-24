pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Layouts
import AD
import AD.Theme
import AD.Components

// The searchable grid of banners: a search box, the continent tabs and the
// country cards. New game and the online lobby both pick from it.
Item {
    id: picker

    property string selectedKey: ""
    property var countries: GameController.allCountries()
    /// Keys that cannot be picked (the opponent's banner, online).
    property var disabledKeys: []
    property string continent: "All"
    signal accepted()

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

    function focusSearch() { search.forceActiveFocus() }
    function isDisabled(key) { return disabledKeys.indexOf(key) >= 0 }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TextField {
                id: search
                Layout.fillWidth: true
                placeholderText: "Search a country or a capital…"
                Keys.onReturnPressed: if (picker.filtered.length > 0) picker.selectedKey = picker.filtered[0].key
            }
            Button {
                text: "Random"
                ToolTip.visible: hovered
                ToolTip.text: "Let fate pick"
                onClicked: {
                    let key = GameController.randomCountryKey()
                    for (let i = 0; i < 8 && picker.isDisabled(key); ++i) key = GameController.randomCountryKey()
                    picker.selectedKey = key
                }
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
                    onClicked: picker.continent = modelData
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
            model: picker.filtered
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: card
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool current: modelData.key === picker.selectedKey
                readonly property bool taken: picker.isDisabled(modelData.key)

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 4
                    radius: Style.radius
                    color: card.current ? Qt.alpha(Style.brass, 0.2)
                         : cardHover.hovered && !card.taken ? Style.slateLight : Style.slate
                    border.width: card.current ? 2 : 1
                    border.color: card.current ? Style.brassBright
                                : cardHover.hovered && !card.taken ? Style.brass : Style.brassDark
                    opacity: card.taken ? 0.35 : 1
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
                            text: card.taken ? "taken"
                                : card.modelData.income + " / round  ·  " + card.modelData.cities
                                  + (card.modelData.cities === 1 ? " city" : " cities")
                            font.pixelSize: Style.fontSmall
                            color: Style.onSlateFaint
                        }
                    }
                    HoverHandler { id: cardHover }
                    TapHandler {
                        enabled: !card.taken
                        onTapped: picker.selectedKey = card.modelData.key
                        onDoubleTapped: { picker.selectedKey = card.modelData.key; picker.accepted() }
                    }
                }
            }
        }

        Text {
            visible: picker.filtered.length === 0
            Layout.alignment: Qt.AlignHCenter
            text: "No country matches"
            color: Style.onSlateFaint
        }
    }
}
