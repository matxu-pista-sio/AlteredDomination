pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import AD
import AD.Theme
import AD.Components

// The ranking (docs/GAME_DESIGN.md §9) over the map's glass: your own
// line pinned at the top, the world beneath it. The header folds it.
GlassPanel {
    id: panel

    property bool expanded: true

    title: "Ranking"
    width: 320
    height: expanded ? 400 : headerHeight + 2
    clip: true
    padding: 8

    Behavior on height {
        enabled: Style.animations
        NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
    }

    MouseArea {
        // the header folds and unfolds the list
        x: 0
        y: -panel.padding - panel.headerHeight
        width: panel.width
        height: panel.headerHeight
        z: 5
        cursorShape: Qt.PointingHandCursor
        onClicked: panel.expanded = !panel.expanded
    }

    component RankRow: Item {
        id: rankRow
        property int rank: 0
        property url flag
        property string name: ""
        property double income: 0
        property int cities: 0
        property double share: 0
        property bool me: false
        property bool eliminated: false
        property bool human: false
        width: parent ? parent.width : 300
        height: 30
        opacity: eliminated ? 0.4 : 1
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: rankRow.me ? Qt.alpha(Style.brass, 0.22) : "transparent"
            border.width: rankRow.me ? 1 : 0
            border.color: Style.brassDark
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: 6
            spacing: 8
            Text {
                width: 22
                anchors.verticalCenter: parent.verticalCenter
                text: rankRow.rank
                font.pixelSize: Style.fontSmall
                font.features: { "tnum": 1 }
                color: Style.onSlateFaint
                horizontalAlignment: Text.AlignRight
            }
            Flag { anchors.verticalCenter: parent.verticalCenter; width: 24; height: 18; source: rankRow.flag }
            Text {
                width: 118
                anchors.verticalCenter: parent.verticalCenter
                text: rankRow.name + (rankRow.human && !rankRow.me ? " ●" : "")
                font.pixelSize: Style.fontSmall + 1
                font.bold: rankRow.me
                color: rankRow.me ? Style.brassBright : Style.onSlate
                elide: Text.ElideRight
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: GameController.formatNumber(rankRow.income) + "  ·  " + rankRow.cities
                    font.pixelSize: Style.fontSmall - 1
                    font.features: { "tnum": 1 }
                    color: Style.onSlateFaint
                }
                Gauge { width: 90; height: 4; value: rankRow.share; maximum: 60; fillColor: rankRow.me ? Style.lamp : Style.brass }
            }
        }
    }

    Column {
        anchors.fill: parent
        spacing: 4

        RankRow {
            rank: GameController.rank
            flag: GameController.humanFlag
            name: GameController.humanName
            income: GameController.income
            cities: GameController.cityCount
            share: GameController.sharePercent
            me: true
        }
        Rectangle { width: parent.width; height: 1; color: Style.brassDark }

        ListView {
            id: list
            width: parent.width
            height: parent.height - 40
            clip: true
            model: GameController.countries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: RankRow {
                required property var model
                width: list.width
                rank: model.rank
                flag: model.flag
                name: model.name
                income: model.income
                cities: model.cities
                share: model.share
                me: model.me
                eliminated: model.eliminated
                human: model.human
            }
        }
    }
}
