# UI theme — the command table

The player is a head of state at a lit table in a dark room. Everything
outside the map is the table: dark slate chrome, brass fittings, amber
lamplight; the map itself is a glowing chart under glass — deep ocean, land
tinted by whoever holds it, cities as lit rings. The board battle is the
same table with a tactical grid projected on it.

## The four roles and the themes

Every chrome colour derives from four ROLES held in the `Style` singleton
(`AD.Theme`, `client/src/style.h`):

| Role | Fills |
|---|---|
| **Ink** | text; deepened, the dark chrome of the table (`slate`, `slateRaised`, `slateLight`) |
| **Paper** | every light surface (`paperDark` for panels, `white` for writing fields) |
| **Brass** | the metal: fittings, borders, focus, the LIT state of a toggle (`brassBright`, `brassDark`) |
| **Lamp** | the signal: primary action and hover (`lampDark`), and `danger` for attack/threat |

WHICH colours fill the roles is the selectable **theme** (settings page,
saved in QSettings, applied live, pinnable for one run with
`AD_THEME=<key>`):

| Theme | Ink | Paper | Brass | Lamp |
|---|---|---|---|---|
| **Situation room** (`situation-room`, default) | `#1b2230` | `#e6e1d6` | `#b3893c` | `#f2a93b` amber |
| **Paper atlas** (`paper-atlas`) | `#2d2620` | `#efe6d2` | `#9a7a3a` | `#c8552e` vermilion |
| **Night ops** (`night-ops`) | `#0d1117` | `#d7dee6` | `#7d8e9e` steel | `#4ee0a3` radar green |

The **map colours are not part of any theme**: ocean (`ocean`, `oceanDeep`),
land (`land`, `landLine`), the link colours (`linkLand`, `linkSea`,
`linkHostile`) and the selection/target colours (`select` cyan, `moveTarget`
green, `attackTarget` red) read the same under every chrome, so a theme
switch never changes what the map means. Country banner colours come from
the world data (DATA_PIPELINE.md §6) and are tinted onto the land.

## Surfaces

- `Panel` — a slate card with a brass hairline; every HUD panel and popup
  is one.
- `GlassPanel` — the same over the map, with a `MultiEffect` blur of what
  lies beneath (the city sheet, the ranking) so the map stays present.
- `PaperOverlay` — procedural grain (`client/shaders/paper.frag`) on paper
  surfaces; a settings switch turns it off (the shader then does not run).
- `Ocean` — `client/shaders/ocean.frag`: slow layered noise with a faint
  graticule; a settings switch reduces it to flat colour.

## The control style

`ADDesktop` (`client/qml/styles/desktop/`) is written from scratch over
`QtQuick.Templates` — no stock Qt style underneath (Basic only as the
fallback for controls the game never instantiates): `Button`, `ComboBox`,
`Slider`, `SpinBox`, `TextField`, `CheckBox`, `Switch`, `ScrollBar`,
`ToolTip`, `Label`, `ItemDelegate`, `Popup`, `TabButton`. Control height
30 px, hover feedback, 1 px borders. `highlighted`/`checked` fills brass
with ink text — the LIT state the HUD toggles rely on; the style's own
`primary` fills with the lamp (the one action a page wants) and `danger`
with the lamp turned red. Pages that use those two import `ADDesktop`
directly, so qmllint sees them.

## Typography

Titles are set in **Rajdhani** (OFL, bundled) — a squared, technical
display face that reads as stencilled metal at 28 px; body copy in
**Source Sans 3** (OFL, bundled). Sizes come from `Style.fontSmall/fontBody/
fontTitle/fontDisplay`; the numbers on the HUD (funds, income, round) ask for
tabular figures (`font.features: { "tnum": 1 }`) so they do not jitter.

## The map stack (WorldMap.qml)

```
Ocean (ShaderEffect, fills the viewport)
└── MapCanvas (Item, scaled/translated by the camera)
    ├── Territories  Repeater → Shape { preferredRendererType: CurveRenderer,
    │                 containsMode: FillContains, ShapePath { PathSvg } }
    │                 one per city, fillColor = owner tint (Behavior: ColorAnimation)
    ├── Borders      one Shape, country outlines, stroke only
    ├── Links        one Shape, land solid / sea dashed / hostile red
    ├── Cities       Repeater → CityMarker (ring by tier, capital star, power badge,
    │                 label with LOD by zoom, HoverHandler + TapHandler)
    └── Overlays     selection pulse, move/attack target rings, fly-to target
```

- Camera: `DragHandler` (pan), `WheelHandler` + `PinchHandler` (zoom about
  the cursor, 0.4×–8×), `flyTo(cityId)` animates both. Arrow keys pan,
  `+`/`-` zoom, `Home` recentres on the capital.
- LOD: tier 3 labels always, tier 2 above 1.2×, tier 1 above 2.2×, tier 0
  above 3.5×; markers scale with `1/sqrt(zoom)` so they never bloat.
- Hover on a territory lights its outline (`select` at 60 %) and the
  city's name; the selected city's move targets glow green, attack targets
  red, with the link between them drawn solid.
- Everything is data-driven from `WorldModel`; nothing in QML knows a
  country name.
- Input: one `TapHandler` on the viewport hit-tests the canvas
  (`childAt`): markers answer through a circular containment mask, then
  territories through `FillContains`; the border and link layers are
  disabled so they never answer. Every `Panel`/`GlassPanel` carries a
  `MouseArea`, so nothing under a panel is ever hit.

## The battle screen

The board stands upright: its long axis runs top to bottom, **the viewing
player always holds the bottom edge** and the enemy the top, whichever
side of the core's `x` axis they are on. `Battle.qml` maps core cells to
screen cells through four functions (`screenRow/screenCol` and
`boardX/boardY`); for the defender the board is turned around
(`mirrored`), so every path, highlight, cursor and projectile is drawn in
screen space from the same mapping. The edge labels name the two
countries above and below the grid.

The screen is three columns: the situation panel on the left (phase title,
the thinking spinner and status line, the city, the enemy's party block
under "▲ the enemy holds the top", the hint text, then "▼ you hold the
bottom" and the player's party block), the board centred in the middle
(cells sized to fit the height, at least 24 px), and the **Orders** panel
on the right (action pips, Ready / End turn, Offer draw, Surrender,
Auto-resolve, Quit, key hints). Arrow keys move a cursor over screen
cells and Space acts on the cell under it.

The board is a `Repeater` of cells over a slate grid, the units are
`UnitChip`s (VectorImage icon on a banner-coloured disc, general crown,
"acted" dimming) that animate between cells (`Behavior on x/y`, 220 ms).
Legal targets of the selected unit come from `BoardModel` (green for
moves, red for strikes, brass for the selection). A strike fires a
`Projectile` (a `Shape` dot along a `PathAnimation`) and an explosion
`ParticleSystem`; a general's fall shakes the board (`combatShake`
setting). Right-click a unit for its `UnitCard` (pattern diagram drawn
from the catalog).

## Rules of thumb

- **No hex colours in QML.** Every colour is a `Style` property; a new
  shade is a new derived property with a name from the table's vocabulary.
- **The map owns green, red and cyan.** Chrome never uses them; a panel
  that did would camouflage a target ring.
- **Lit means brass.** Selection, engaged toggles and focus all read as
  the theme's metal catching lamplight; danger reads as the lamp turned
  red.
- Every screen fits a 1280×800 window and grows to fill 4K; the window's
  minimum is 1024×640.
