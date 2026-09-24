import QtQuick
import QtQuick.Controls

// Placeholder shell: the real pages (Home, New game, Campaign, Battle)
// arrive with the theme and shell tickets. This window proves the build,
// the module and the Qt version.
ApplicationWindow {
    id: window
    width: 1280
    height: 800
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: qsTr("Altered Domination")
    color: "#1b2230"

    Column {
        anchors.centerIn: parent
        spacing: 12

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Altered Domination")
            color: "#e6e1d6"
            font.pixelSize: 40
            font.bold: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("v%1 · Qt %2").arg(Qt.application.version).arg(qtVersion())
            color: "#b3893c"
            font.pixelSize: 16

            function qtVersion() {
                // Qt.application has no version of Qt itself; the C++ side
                // will expose it through Style once the theme lands.
                return "6"
            }
        }
    }
}
