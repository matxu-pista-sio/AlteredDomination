import QtQuick
import QtQuick.Templates as T
import AD.Theme

// One line of a paper list (a dropdown's sheet): ink on paper, a brass
// wash under the highlighted line.
T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 5
    horizontalPadding: 10

    font.pixelSize: Style.fontBody

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Style.ink : Style.inkFaint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitHeight: Style.controlHeight - 4
        radius: Style.radius - 2
        color: control.down ? Style.brass
             : control.highlighted || control.hovered
               ? Qt.alpha(Style.brass, 0.30) : "transparent"
    }
}
