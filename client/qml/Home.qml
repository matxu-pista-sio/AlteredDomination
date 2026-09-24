pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import ADDesktop
import QtQuick.Shapes
import AD
import AD.Theme
import AD.Components

// The home screen: the chart itself drifts under glass behind a lit slate
// menu - the same world data the campaign draws, tinted by banner colour,
// so the first thing seen is the game.
FocusScope {
    id: home

    signal newGame()
    signal loadGame()
    signal codex()
    signal settings()
    signal gallery()
    signal quit()

    focus: true
    Component.onCompleted: newGameButton.forceActiveFocus()

    Rectangle { anchors.fill: parent; color: Style.oceanDeep }

    Item {
        id: world
        anchors.fill: parent
        clip: true

        Item {
            id: canvas
            readonly property real fit: Math.max(home.width / GameController.mapWidth,
                                                 home.height / GameController.mapHeight) * 1.25
            width: GameController.mapWidth
            height: GameController.mapHeight
            scale: fit
            transformOrigin: Item.TopLeft
            y: -(GameController.mapHeight * fit - home.height) * 0.45
            x: -(GameController.mapWidth * fit - home.width) * 0.35

            SequentialAnimation on x {
                loops: Animation.Infinite
                running: Style.animations
                NumberAnimation { to: -(GameController.mapWidth * canvas.fit - home.width) * 0.62; duration: 90000; easing.type: Easing.InOutSine }
                NumberAnimation { to: -(GameController.mapWidth * canvas.fit - home.width) * 0.35; duration: 90000; easing.type: Easing.InOutSine }
            }

            Repeater {
                model: GameController.cities
                Shape {
                    id: territory
                    required property string territoryPath
                    required property color ownerColor
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        fillColor: Qt.alpha(Qt.tint(Style.land, Qt.alpha(territory.ownerColor, 0.7)), 0.85)
                        strokeColor: Qt.alpha(Style.landLine, 0.55)
                        strokeWidth: 0.7
                        PathSvg { path: territory.territoryPath }
                    }
                }
            }
        }
    }

    // the glass: a lamp-lit pool that dims toward the edges and the menu
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.alpha(Style.slate, 0.96) }
            GradientStop { position: 0.36; color: Qt.alpha(Style.slate, 0.80) }
            GradientStop { position: 0.7; color: Qt.alpha(Style.slate, 0.30) }
            GradientStop { position: 1.0; color: Qt.alpha(Style.slate, 0.55) }
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.alpha(Style.slate, 0.55) }
            GradientStop { position: 0.3; color: "transparent" }
            GradientStop { position: 0.75; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.alpha(Style.slate, 0.7) }
        }
    }

    Column {
        id: menu
        x: Math.max(64, home.width * 0.07)
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        width: 300

        Image {
            source: "qrc:/assets/branding/adlogo.svg"
            width: 120
            fillMode: Image.PreserveAspectFit
            sourceSize: Qt.size(240, 160)
        }
        Item { width: 1; height: 4 }
        Text {
            text: "ALTERED"
            font.family: Style.displayFamily
            font.pixelSize: 48
            font.bold: true
            font.letterSpacing: 8
            color: Style.paper
            lineHeight: 0.85
        }
        Text {
            text: "DOMINATION"
            font.family: Style.displayFamily
            font.pixelSize: 48
            font.bold: true
            font.letterSpacing: 8
            color: Style.brassBright
            lineHeight: 0.85
        }
        Text {
            text: "the world under one banner"
            font.pixelSize: Style.fontBody + 1
            font.letterSpacing: 2.5
            color: Style.onSlateFaint
        }
        PipDivider { width: 260 }
        Item { width: 1; height: 6 }

        Button {
            id: newGameButton
            width: 260
            height: 44
            text: "New game"
            primary: true
            onClicked: home.newGame()
            KeyNavigation.down: loadButton
        }
        Button {
            id: loadButton
            width: 260
            height: 40
            text: "Load game"
            enabled: GameController.saves.count > 0
            ToolTip.visible: hovered && !enabled
            ToolTip.text: "No saved campaigns yet"
            onClicked: home.loadGame()
            KeyNavigation.up: newGameButton
            KeyNavigation.down: codexButton
        }
        Button {
            id: codexButton
            width: 260
            height: 40
            text: "Codex"
            onClicked: home.codex()
            KeyNavigation.up: loadButton
            KeyNavigation.down: settingsButton
        }
        Button {
            id: settingsButton
            width: 260
            height: 40
            text: "Settings"
            onClicked: home.settings()
            KeyNavigation.up: codexButton
            KeyNavigation.down: quitButton
        }
        Button {
            id: quitButton
            width: 260
            height: 40
            text: "Quit"
            onClicked: home.quit()
            KeyNavigation.up: settingsButton
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 18
        text: "v" + Style.version + "  ·  Qt " + Style.qtVersion
        font.pixelSize: Style.fontSmall
        color: Style.onSlateFaint
        TapHandler {
            // a quiet door to the control gallery (issue #12)
            acceptedModifiers: Qt.ControlModifier
            onTapped: home.gallery()
        }
    }

    Image {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        source: "qrc:/assets/branding/afkaarlogo.svg"
        width: 110
        fillMode: Image.PreserveAspectFit
        sourceSize: Qt.size(220, 60)
        opacity: 0.35
    }

    Shortcut { sequence: "Ctrl+G"; onActivated: home.gallery() }
}
