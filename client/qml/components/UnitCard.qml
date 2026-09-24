import QtQuick
import QtQuick.Controls
import AD.Theme

// A unit's card on paper: what it is, what it costs, what it can affect,
// and its pattern diagram drawn from the catalog.
Popup {
    id: card

    property var info: ({})
    property string state: ""

    modal: false
    focus: false
    parent: Overlay.overlay
    width: 420
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    contentItem: Row {
        spacing: 18
        Column {
            width: 200
            spacing: 8
            Row {
                spacing: 10
                Rectangle {
                    width: 44; height: 44; radius: 22
                    color: Style.brass
                    border.width: 1
                    border.color: Style.brassDark
                    Image { anchors.fill: parent; anchors.margins: 6; source: card.info.icon || ""; sourceSize: Qt.size(88, 88) }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: card.info.name || ""
                        font.family: Style.displayFamily
                        font.pixelSize: Style.fontTitle
                        font.bold: true
                        color: Style.ink
                    }
                    Text {
                        text: "Cost " + (card.info.cost || 0) + "  ·  " + (card.info.classText || "")
                        font.pixelSize: Style.fontSmall
                        color: Style.lampDark
                    }
                }
            }
            Text {
                width: parent.width
                text: card.info.description || ""
                font.pixelSize: Style.fontSmall + 1
                color: Style.ink
                wrapMode: Text.Wrap
            }
            Text {
                visible: card.state !== ""
                width: parent.width
                text: card.state
                font.pixelSize: Style.fontSmall
                font.bold: true
                color: Style.inkFaint
                wrapMode: Text.Wrap
            }
        }
        Column {
            spacing: 6
            PatternDiagram {
                moves: card.info.moves || []
                strikes: card.info.strikes || []
                icon: card.info.icon || ""
                cellSize: 16
            }
            Text {
                text: "the enemy is upward"
                font.pixelSize: Style.fontSmall - 1
                color: Style.inkFaint
            }
        }
    }
}
