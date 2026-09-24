pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import AD
import AD.Theme
import AD.Components

// F: find a city or a country by name and fly there.
Popup {
    id: search

    signal picked(int id)

    modal: true
    focus: true
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: 90
    width: 460
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var results: []

    function pick(id) { search.picked(id); search.close() }

    onOpened: { query.text = ""; query.forceActiveFocus() }

    contentItem: Column {
        spacing: 8
        TextField {
            id: query
            width: parent.width
            placeholderText: "City or country…"
            onTextChanged: search.results = GameController.searchCities(text, 10)
            Keys.onReturnPressed: if (search.results.length > 0) search.pick(search.results[0].id)
            Keys.onEnterPressed: if (search.results.length > 0) search.pick(search.results[0].id)
        }
        Repeater {
            model: search.results
            Rectangle {
                id: hit
                required property var modelData
                required property int index
                width: parent.width
                height: 32
                radius: 4
                color: hitHover.hovered || index === 0 ? Qt.alpha(Style.brass, 0.25) : "transparent"
                HoverHandler { id: hitHover }
                TapHandler { onTapped: search.pick(hit.modelData.id) }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 6
                    spacing: 10
                    Flag { width: 24; height: 18; anchors.verticalCenter: parent.verticalCenter; source: hit.modelData.flag }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: hit.modelData.name + (hit.modelData.capital ? " ★" : "")
                        font.pixelSize: Style.fontBody
                        font.bold: hit.modelData.mine
                        color: Style.ink
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: hit.modelData.countryName + "  ·  held by " + hit.modelData.ownerName
                        font.pixelSize: Style.fontSmall
                        color: Style.inkFaint
                    }
                }
            }
        }
        Text {
            visible: query.text !== "" && search.results.length === 0
            text: "Nothing by that name"
            font.pixelSize: Style.fontSmall
            color: Style.inkFaint
        }
    }
}
