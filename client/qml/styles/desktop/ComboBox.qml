pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import AD.Theme

// A field, not a button: the closed control reads like the TextField
// beside it, and opening it unrolls a paper sheet of choices under a
// brass chevron.
T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 4
    leftPadding: 10
    rightPadding: 28

    font.pixelSize: Style.fontBody

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Style.ink : Style.inkFaint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 9
        y: (control.height - height) / 2
        text: "▾"
        font.pixelSize: 14
        color: control.enabled
               ? (control.hovered || control.popup.visible ? Style.brass : Style.brassDark)
               : Style.inkFaint
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: 120 } }
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: Style.controlHeight
        radius: Style.radius
        color: control.enabled ? Style.white : Style.paperDark
        border.width: control.activeFocus || control.popup.visible ? 2 : 1
        border.color: control.activeFocus || control.popup.visible ? Style.brass
                    : control.hovered ? Style.brass : Style.brassDark
        opacity: control.enabled ? 1.0 : 0.7
    }

    delegate: ItemDelegate {
        required property var model
        required property var modelData
        required property int index

        width: ListView.view ? ListView.view.width : control.width
        text: control.textRole
              ? (Array.isArray(control.model)
                 ? modelData[control.textRole] : model[control.textRole])
              : modelData
        highlighted: control.highlightedIndex === index
    }

    popup: T.Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding
                                 + bottomPadding, 340)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds

            T.ScrollBar.vertical: ScrollBar { }
        }

        background: Rectangle {
            radius: Style.radius
            color: Style.paper
            border.width: 1
            border.color: Style.brassDark

            Rectangle {
                z: -1
                anchors.fill: parent
                anchors.topMargin: 3
                anchors.leftMargin: 1
                anchors.rightMargin: -1
                radius: parent.radius
                color: Qt.alpha(Style.ink, 0.45)
            }
        }
    }
}
