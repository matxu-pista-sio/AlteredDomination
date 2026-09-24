import QtQuick
import QtQuick.VectorImage
import AD.Theme

// A country's flag (assets/flags, 4:3), drawn as vectors by the curve
// renderer so it stays crisp at any size, under a brass hairline.
Item {
    id: flag

    property url source
    property bool bordered: true

    implicitWidth: 40
    implicitHeight: 30

    VectorImage {
        anchors.fill: parent
        source: flag.source
        preferredRendererType: VectorImage.CurveRenderer
        fillMode: VectorImage.Stretch
    }
    Rectangle {
        visible: flag.bordered
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Qt.alpha(Style.brassDark, 0.9)
    }
}
