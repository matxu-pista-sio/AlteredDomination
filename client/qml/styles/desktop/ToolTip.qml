import QtQuick
import QtQuick.Templates as T
import AD.Theme

// A field note pinned above the control: paper text on raised slate,
// brass rim.
T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - 6

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    margins: 8
    padding: 8
    horizontalPadding: 11
    delay: 500

    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
                 | T.Popup.CloseOnReleaseOutsideParent

    font.pixelSize: Style.fontSmall

    contentItem: Text {
        text: control.text
        font: control.font
        color: Style.onSlate
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        radius: Style.radius
        color: Qt.rgba(Style.slateRaised.r, Style.slateRaised.g, Style.slateRaised.b, 0.97)
        border.width: 1
        border.color: Style.brassDark
    }
}
