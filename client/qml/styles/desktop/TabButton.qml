import QtQuick
import QtQuick.Templates as T
import AD.Theme

// A tab: display type over a hairline that lights brass when checked.
T.TabButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    horizontalPadding: 14

    font.family: Style.displayFamily
    font.pixelSize: Style.fontBody + 2
    font.bold: true
    font.letterSpacing: 1.2
    font.capitalization: Font.AllUppercase

    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: !control.enabled ? Style.onSlateFaint
             : control.checked ? Style.brassBright
             : control.hovered ? Style.onSlate : Style.onSlateFaint
        Behavior on color { ColorAnimation { duration: 100 } }
    }

    background: Item {
        implicitHeight: Style.controlHeight + 4
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: control.checked ? 2 : 1
            color: control.checked ? Style.brass : control.visualFocus ? Style.lamp : Style.brassDark
            opacity: control.checked || control.hovered || control.visualFocus ? 1 : 0.5
        }
    }
}
