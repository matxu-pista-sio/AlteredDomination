pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Layouts
import AD
import AD.Theme
import AD.Components

// The save slots as cards; the autosave comes first.
FocusScope {
    id: page

    signal back()
    signal loaded()

    focus: true
    Keys.onEscapePressed: page.back()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        RowLayout {
            spacing: 16
            BackButton { onClicked: page.back() }
            PageTitle { text: "Load game"; subtitle: GameController.saves.directory }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 12

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                spacing: 8
                model: GameController.saves
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: card
                    required property string slot
                    required property string title
                    required property string countryName
                    required property url flag
                    required property int round
                    required property double share
                    required property string date
                    required property string mode
                    required property string difficulty
                    required property string error
                    required property bool autosave
                    required property int playSeconds

                    width: list.width
                    height: 84
                    radius: Style.radius
                    color: cardHover.hovered ? Style.slateLight : Style.slate
                    border.width: 1
                    border.color: card.error !== "" ? Style.danger : cardHover.hovered ? Style.brass : Style.brassDark
                    HoverHandler { id: cardHover }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 14

                        Flag {
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 48
                            source: card.flag
                            visible: card.flag.toString() !== ""
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                spacing: 8
                                Text {
                                    text: card.title
                                    font.family: Style.displayFamily
                                    font.pixelSize: Style.fontBody + 3
                                    font.bold: true
                                    color: Style.onSlate
                                }
                                Text {
                                    visible: card.error === ""
                                    text: card.countryName
                                    font.pixelSize: Style.fontBody
                                    color: Style.brassBright
                                }
                            }
                            Text {
                                visible: card.error === ""
                                text: "Round " + card.round + "  ·  " + card.share.toFixed(1) + " % of the world  ·  "
                                      + card.mode + " / " + card.difficulty
                                      + (card.playSeconds > 0 ? "  ·  " + Math.floor(card.playSeconds / 60) + " min" : "")
                                font.pixelSize: Style.fontSmall + 1
                                color: Style.onSlateFaint
                            }
                            Text {
                                visible: card.error !== ""
                                text: "This save cannot be read (" + card.error + ")"
                                font.pixelSize: Style.fontSmall + 1
                                color: Style.danger
                            }
                            Gauge {
                                visible: card.error === ""
                                Layout.preferredWidth: 220
                                value: card.share
                                threshold: GameController.dominationPercent
                            }
                        }
                        Text {
                            text: card.date
                            font.pixelSize: Style.fontSmall
                            color: Style.onSlateFaint
                        }
                        Button {
                            text: "Load"
                            primary: true
                            enabled: card.error === ""
                            onClicked: if (GameController.loadGame(card.slot)) page.loaded()
                        }
                        IconButton {
                            glyph: "✕"
                            tip: "Delete this save"
                            onClicked: { confirmDelete.slot = card.slot; confirmDelete.open() }
                        }
                    }
                }
            }

            Text {
                visible: GameController.saves.count === 0
                anchors.centerIn: parent
                text: "No saved campaigns yet"
                font.pixelSize: Style.fontTitle
                color: Style.onSlateFaint
            }
        }
    }

    ConfirmDialog {
        id: confirmDelete
        property string slot: ""
        title: "Delete this save?"
        text: "\"" + slot + "\" will be removed. This cannot be undone."
        confirmText: "Delete"
        destructive: true
        onAccepted: GameController.saves.remove(slot)
    }
}
