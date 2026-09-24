pragma ComponentBehavior: Bound
import QtQuick
import AD.Theme

// Three brass pips between two hairlines: the divider between sections.
Row {
    id: divider

    property int pips: 3

    spacing: 8
    height: 6

    Rectangle {
        width: (divider.width - pipRow.width - divider.spacing * 2) / 2
        height: 1
        anchors.verticalCenter: parent.verticalCenter
        color: Style.brassDark
    }
    Row {
        id: pipRow
        spacing: 6
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            model: divider.pips
            Rectangle {
                width: 5
                height: 5
                radius: 2.5
                color: Style.brass
            }
        }
    }
    Rectangle {
        width: (divider.width - pipRow.width - divider.spacing * 2) / 2
        height: 1
        anchors.verticalCenter: parent.verticalCenter
        color: Style.brassDark
    }
}
