pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import AD
import AD.Theme
import AD.Components

// Escape: resume, save to a slot, settings, or quit to the menu.
Popup {
    id: menu

    signal openSettings()
    signal quitToMenu()
    signal saved(string slot)

    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 420
    closePolicy: Popup.CloseOnEscape

    property bool saving: false

    function quickSave() {
        if (GameController.online) return
        const slot = GameController.currentSlot !== "" && GameController.currentSlot !== "autosave"
                     ? GameController.currentSlot : GameController.saves.newSlotName()
        if (GameController.saveGame(slot)) menu.saved(slot)
    }

    onOpened: { saving = false; resumeButton.forceActiveFocus() }

    contentItem: Column {
        spacing: 10
        Text {
            text: "Paused"
            font.family: Style.displayFamily
            font.pixelSize: Style.fontTitle
            font.bold: true
            color: Style.ink
        }
        Text {
            width: parent.width
            text: GameController.humanName + (GameController.online ? "  vs  " + GameController.opponentName : "")
                  + "  ·  round " + GameController.round + "  ·  " + GameController.modeKey
                  + " / " + GameController.difficultyKey + "  ·  seed " + GameController.seedText
            font.pixelSize: Style.fontSmall
            color: Style.inkFaint
            wrapMode: Text.Wrap
        }
        Rectangle { width: parent.width; height: 1; color: Style.brassDark }

        Button {
            id: resumeButton
            width: parent.width
            text: "Resume"
            primary: true
            onClicked: menu.close()
        }
        Button {
            visible: !GameController.online
            width: parent.width
            text: menu.saving ? "Save to…" : "Save"
            checkable: true
            checked: menu.saving
            enabled: GameController.humanTurn
            onClicked: menu.saving = !menu.saving
        }
        Button {
            visible: GameController.online && !GameController.onlineOver
            width: parent.width
            text: "Resign the match"
            danger: true
            onClicked: confirmResign.open()
        }
        Column {
            visible: menu.saving
            width: parent.width
            spacing: 6
            Row {
                spacing: 6
                TextField {
                    id: slotName
                    width: menu.width - menu.leftPadding - menu.rightPadding - 90
                    text: GameController.currentSlot !== "" && GameController.currentSlot !== "autosave"
                          ? GameController.currentSlot : GameController.saves.newSlotName()
                    placeholderText: "slot name"
                    validator: RegularExpressionValidator { regularExpression: /[A-Za-z0-9_-]{1,40}/ }
                }
                Button {
                    text: "Save"
                    primary: true
                    width: 80
                    enabled: slotName.acceptableInput
                    onClicked: if (GameController.saveGame(slotName.text)) { menu.saved(slotName.text); menu.close() }
                }
            }
            Text {
                text: GameController.saves.exists(slotName.text) ? "Overwrites the existing slot" : "A new slot"
                font.pixelSize: Style.fontSmall
                color: Style.inkFaint
            }
        }
        Button {
            width: parent.width
            text: "Settings"
            onClicked: { menu.close(); menu.openSettings() }
        }
        Button {
            width: parent.width
            text: "Quit to the menu"
            danger: true
            onClicked: GameController.online && !GameController.onlineOver ? confirmResign.open() : confirmQuit.open()
        }
        Text {
            width: parent.width
            text: GameController.online ? "An online match is kept on the server; leaving it concedes."
                                        : "Ctrl+S saves to the current slot at any time."
            font.pixelSize: Style.fontSmall
            color: Style.inkFaint
        }
    }

    ConfirmDialog {
        id: confirmResign
        title: "Resign the match?"
        text: "The server records a loss for you and a win for " + GameController.opponentName + "."
        confirmText: "Resign"
        destructive: true
        onAccepted: { menu.close(); GameController.resign() }
    }
    ConfirmDialog {
        id: confirmQuit
        title: "Quit to the menu?"
        text: "Progress since the last save or autosave is lost."
        confirmText: "Quit"
        destructive: true
        onAccepted: { menu.close(); menu.quitToMenu() }
    }
}
