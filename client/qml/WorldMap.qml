pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import AD
import AD.Theme

// The chart under glass (docs/UI_THEME.md "The map stack"): the ocean
// shader in the viewport, and inside a camera-driven canvas the
// territories (one curve-rendered Shape per city, tinted by owner), the
// country borders (one Shape), the links (one Shape per kind) and the
// city markers. Everything is data-driven from WorldModel and LinkModel;
// nothing here knows a country's name.
Item {
    id: map

    signal cityClicked(int id)
    signal cityRightClicked(int id)
    signal emptyClicked()

    property int hoveredCity: -1

    // -- camera --------------------------------------------------------------------
    readonly property real mapW: GameController.mapWidth
    readonly property real mapH: GameController.mapHeight
    readonly property real fitZoom: Math.max(0.01, Math.min(width / mapW, height / mapH))
    readonly property real minZoom: fitZoom * 0.9
    readonly property real maxZoom: fitZoom * 14
    property real zoom: fitZoom
    property real originX: (width - mapW * fitZoom) / 2
    property real originY: (height - mapH * fitZoom) / 2
    /// How far in we are, relative to the whole world fitting the viewport.
    readonly property real zoomLevel: zoom / fitZoom
    /// Marker items are authored in px at fit; this keeps them growing with
    /// the square root of the zoom.
    readonly property real markerScale: Math.sqrt(fitZoom / zoom) / fitZoom
    /// Stroke widths in map units, quantised to powers of two of the zoom so
    /// the curve renderer re-tessellates only at level changes.
    readonly property real hairline: 1.0 / (fitZoom * Math.pow(2, Math.round(Math.log2(zoomLevel))))

    /// What lies under a viewport point: a city id (a marker or the territory
    /// it sits in), or -1 for open water. Markers sit above territories;
    /// the border and link layers are disabled so they never answer.
    function cityAt(vx, vy) {
        const p = canvas.mapFromItem(map, vx, vy)
        const item = canvas.childAt(p.x, p.y)
        return item && item.cityId !== undefined ? item.cityId : -1
    }

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    function clampOrigin() {
        originX = clamp(originX, width * 0.3 - mapW * zoom, width * 0.7)
        originY = clamp(originY, height * 0.3 - mapH * zoom, height * 0.7)
    }
    function zoomAt(cx, cy, factor) {
        flight.stop()
        const z = clamp(zoom * factor, minZoom, maxZoom)
        const f = z / zoom
        originX = cx - (cx - originX) * f
        originY = cy - (cy - originY) * f
        zoom = z
        clampOrigin()
    }
    function panBy(dx, dy) {
        flight.stop()
        originX += dx
        originY += dy
        clampOrigin()
    }
    function fitWorld() {
        flyToRect(fitZoom, (width - mapW * fitZoom) / 2, (height - mapH * fitZoom) / 2)
    }
    function flyTo(cityId) {
        if (cityId < 0) return
        const c = GameController.cityInfo(cityId)
        const z = Math.max(zoom, fitZoom * 3.2)
        flyToRect(z, width / 2 - c.x * z, height / 2 - c.y * z)
    }
    function flyToRect(z, ox, oy) {
        flight.stop()
        if (!Style.animations) {
            zoom = z; originX = ox; originY = oy
            return
        }
        flight.targetZoom = z
        flight.targetX = ox
        flight.targetY = oy
        flight.start()
    }
    function screenPos(cityId) {
        const c = GameController.cityInfo(cityId)
        return Qt.point(originX + c.x * zoom, originY + c.y * zoom)
    }

    ParallelAnimation {
        id: flight
        property real targetZoom: 1
        property real targetX: 0
        property real targetY: 0
        NumberAnimation { target: map; property: "zoom"; to: flight.targetZoom; duration: 650; easing.type: Easing.InOutCubic }
        NumberAnimation { target: map; property: "originX"; to: flight.targetX; duration: 650; easing.type: Easing.InOutCubic }
        NumberAnimation { target: map; property: "originY"; to: flight.targetY; duration: 650; easing.type: Easing.InOutCubic }
    }

    // -- the water --------------------------------------------------------------------
    Ocean {
        anchors.fill: parent
        mapSize: Qt.vector2d(map.width / map.zoom, map.height / map.zoom)
        mapOrigin: Qt.vector2d(-map.originX / map.zoom, -map.originY / map.zoom)
        zoom: map.zoom
    }

    // -- input ------------------------------------------------------------------------
    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        property real startX: 0
        property real startY: 0
        onActiveChanged: {
            if (active) { flight.stop(); startX = map.originX; startY = map.originY }
        }
        onActiveTranslationChanged: {
            if (!active) return
            map.originX = startX + activeTranslation.x
            map.originY = startY + activeTranslation.y
            map.clampOrigin()
        }
    }
    WheelHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            const steps = event.angleDelta.y / 120
            map.zoomAt(event.x, event.y, Math.pow(1.25, steps))
        }
    }
    PinchHandler {
        target: null
        property real startZoom: 1
        onActiveChanged: if (active) startZoom = map.zoom
        onActiveScaleChanged: if (active) map.zoomAt(centroid.position.x, centroid.position.y, startZoom * activeScale / map.zoom)
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: (eventPoint) => {
            const id = map.cityAt(eventPoint.position.x, eventPoint.position.y)
            if (id >= 0) map.cityClicked(id)
            else map.emptyClicked()
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint) => {
            const id = map.cityAt(eventPoint.position.x, eventPoint.position.y)
            if (id >= 0) map.cityRightClicked(id)
        }
    }

    // -- the canvas -------------------------------------------------------------------
    Item {
        id: canvas
        x: map.originX
        y: map.originY
        width: map.mapW
        height: map.mapH
        scale: map.zoom
        transformOrigin: Item.TopLeft

        // territories
        Repeater {
            model: GameController.cities
            Shape {
                id: territory
                required property int id
                readonly property int cityId: id
                required property string territoryPath
                required property color ownerColor
                required property int highlight
                required property bool selected
                required property bool mine
                width: map.mapW
                height: map.mapH
                preferredRendererType: Shape.CurveRenderer
                containsMode: Shape.FillContains
                readonly property bool lit: hover.hovered
                readonly property bool marked: selected || highlight > 0

                ShapePath {
                    fillColor: Qt.tint(Style.land, Qt.alpha(territory.ownerColor, 0.62))
                    strokeColor: territory.selected ? Style.select
                               : territory.highlight === 1 ? Style.moveTarget
                               : territory.highlight === 2 ? Style.attackTarget
                               : territory.lit ? Qt.alpha(Style.select, 0.6) : "transparent"
                    strokeWidth: territory.marked ? map.hairline * 2.2 : territory.lit ? map.hairline * 1.6 : -1
                    joinStyle: ShapePath.RoundJoin
                    capStyle: ShapePath.RoundCap
                    Behavior on fillColor {
                        enabled: Style.animations
                        ColorAnimation { duration: 600 }
                    }
                    PathSvg { path: territory.territoryPath }
                }

                HoverHandler {
                    id: hover
                    onHoveredChanged: {
                        if (hovered) map.hoveredCity = territory.id
                        else if (map.hoveredCity === territory.id) map.hoveredCity = -1
                    }
                }
            }
        }

        // country borders
        Shape {
            width: map.mapW
            height: map.mapH
            enabled: false
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: "transparent"
                strokeColor: Qt.alpha(Style.landLine, 0.75)
                strokeWidth: map.hairline * 1.1
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: GameController.bordersPath }
            }
        }

        // links: land solid, sea dashed, hostile red, the selection's active ones lit
        Shape {
            width: map.mapW
            height: map.mapH
            enabled: false
            ShapePath {
                fillColor: "transparent"
                strokeColor: Qt.alpha(Style.linkLand, 0.32)
                strokeWidth: map.hairline * 0.9
                PathSvg { path: GameController.links.landPath }
            }
        }
        Shape {
            width: map.mapW
            height: map.mapH
            enabled: false
            ShapePath {
                fillColor: "transparent"
                strokeColor: Qt.alpha(Style.linkSea, 0.45)
                strokeWidth: map.hairline * 0.9
                strokeStyle: ShapePath.DashLine
                dashPattern: [5, 4]
                PathSvg { path: GameController.links.seaPath }
            }
        }
        Shape {
            width: map.mapW
            height: map.mapH
            enabled: false
            ShapePath {
                fillColor: "transparent"
                strokeColor: Qt.alpha(Style.linkHostile, 0.6)
                strokeWidth: map.hairline * 1.1
                PathSvg { path: GameController.links.hostilePath }
            }
        }
        Shape {
            width: map.mapW
            height: map.mapH
            enabled: false
            ShapePath {
                fillColor: "transparent"
                strokeColor: Style.select
                strokeWidth: map.hairline * 2.4
                capStyle: ShapePath.RoundCap
                PathSvg { path: GameController.links.activePath }
            }
        }

        // cities
        Repeater {
            model: GameController.cities
            CityMarker {
                id: city
                required property var model
                required property int index
                readonly property int cityId: index
                x: model.x - width / 2
                y: model.y - height / 2
                scale: map.markerScale
                name: model.name
                tier: model.tier
                capital: model.capital
                ownerColor: model.ownerColor
                unitCount: model.unitCount
                power: model.power
                highlight: model.highlight
                selected: model.selected
                mine: model.mine
                hovered: map.hoveredCity === index
                showLabel: Style.showLabels && (model.tier >= 3
                           || (model.tier === 2 && map.zoomLevel >= 1.6)
                           || (model.tier === 1 && map.zoomLevel >= 3.6)
                           || map.zoomLevel >= 6)
                showPower: map.zoomLevel >= 2.8 || model.mine || model.selected
                z: (model.selected ? 3 : 0) + (model.highlight > 0 ? 2 : 0) + (hovered ? 1 : 0)
            }
        }
    }
}
