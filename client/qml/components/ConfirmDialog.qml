import QtQuick
import QtQuick.Controls
import ADDesktop
import AD.Theme

// A question on a paper sheet: title, one paragraph, Cancel and the
// action. `destructive` turns the action red.
Popup {
    id: dialog

    property string title: ""
    property string text: ""
    property string confirmText: "Confirm"
    property string cancelText: "Cancel"
    property bool destructive: false

    signal accepted()

    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 420
    closePolicy: Popup.CloseOnEscape

    contentItem: Column {
        spacing: 14
        Text {
            width: parent.width
            text: dialog.title
            font.family: Style.displayFamily
            font.pixelSize: Style.fontTitle
            font.bold: true
            color: Style.ink
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            text: dialog.text
            font.pixelSize: Style.fontBody
            color: Style.ink
            wrapMode: Text.Wrap
        }
        Row {
            anchors.right: parent.right
            spacing: Style.spacing
            Button {
                text: dialog.cancelText
                onClicked: dialog.close()
            }
            Button {
                id: okButton
                text: dialog.confirmText
                primary: !dialog.destructive
                danger: dialog.destructive
                focus: true
                onClicked: { dialog.accepted(); dialog.close() }
            }
        }
    }

    onOpened: okButton.forceActiveFocus()
}
