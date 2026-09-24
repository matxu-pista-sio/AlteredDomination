pragma ComponentBehavior: Bound
import QtQuick
import AD.Theme

// A unit's move and strike patterns drawn on a grid around it, the enemy
// upward: green cells are moves, red cells strikes, a dot marks a cell
// that must be empty on the way. Drawn from the catalog, never from a
// picture.
Item {
    id: diagram

    property var moves: []
    property var strikes: []
    property url icon
    property color banner: Style.brass
    property int cellSize: 20

    readonly property int reach: {
        let r = 1
        for (const m of moves) r = Math.max(r, Math.abs(m.dx), Math.abs(m.dy))
        for (const s of strikes) r = Math.max(r, Math.abs(s.dx), Math.abs(s.dy))
        return r
    }
    readonly property int cells: reach * 2 + 1
    readonly property var marks: {
        const out = {}
        const key = (dx, dy) => dx + "," + dy
        for (const m of moves) {
            for (const p of m.path) if (!out[key(p.dx, p.dy)]) out[key(p.dx, p.dy)] = 4
            out[key(m.dx, m.dy)] = 1
        }
        for (const s of strikes) {
            for (const p of s.path) if (!out[key(p.dx, p.dy)]) out[key(p.dx, p.dy)] = 4
            const k = key(s.dx, s.dy)
            out[k] = out[k] === 1 ? 3 : 2
        }
        return out
    }

    implicitWidth: cells * cellSize
    implicitHeight: cells * cellSize

    Grid {
        columns: diagram.cells
        Repeater {
            model: diagram.cells * diagram.cells
            Rectangle {
                required property int index
                readonly property int dy: index % diagram.cells - diagram.reach
                readonly property int dx: diagram.reach - Math.floor(index / diagram.cells)
                readonly property int mark: diagram.marks[dx + "," + dy] || 0
                width: diagram.cellSize
                height: diagram.cellSize
                color: mark === 1 ? Qt.alpha(Style.moveTarget, 0.55)
                     : mark === 2 ? Qt.alpha(Style.attackTarget, 0.6)
                     : mark === 3 ? Qt.alpha(Style.moveTarget, 0.55)
                     : (dx + dy) % 2 === 0 ? Style.boardLight : Style.boardDark
                border.width: mark === 3 ? 2 : 1
                border.color: mark === 3 ? Style.attackTarget : Qt.alpha(Style.ink, 0.35)
                Rectangle {
                    visible: parent.mark === 4
                    anchors.centerIn: parent
                    width: 5
                    height: 5
                    radius: 2.5
                    color: Style.paper
                    opacity: 0.7
                }
            }
        }
    }

    Rectangle {
        x: diagram.reach * diagram.cellSize + 1
        y: diagram.reach * diagram.cellSize + 1
        width: diagram.cellSize - 2
        height: diagram.cellSize - 2
        radius: width / 2
        color: diagram.banner
        border.width: 1
        border.color: Style.paper
        Image {
            anchors.fill: parent
            anchors.margins: 2
            source: diagram.icon
            sourceSize: Qt.size(48, 48)
            fillMode: Image.PreserveAspectFit
        }
    }
}
