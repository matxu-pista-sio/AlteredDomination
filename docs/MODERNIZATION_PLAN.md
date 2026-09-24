# Altered Domination — modernization audit and plan

Status: **executed** — milestones M0–M8 (issues 01–19) are done on branch
`claude/dreamy-volta-pvb4kh`; each ticket under `docs/issues/` carries its
done-record. Open: the owner items 20–22. Originally: **plan of record**. This document is the audit of the 2018 code base
(qmake, Qt 5.x, QGraphicsView + QtWidgets + six floating `QQuickView` windows)
and the plan that turns it into a 2026 desktop game built the way
[TopGen](https://github.com/matxu-pista-sio/TopGen) is built: a deterministic,
Qt-free C++23 **core** with doctest coverage, a **pure QML** Qt 6 client over
a thin C++ bridge, generated data and assets, CMake presets, CI, and design
docs that are normative for the code.

Every item below is a ticket in [docs/issues/](issues/README.md) (§4 explains why they are not on GitHub yet).
The plan is executed issue by issue on the `claude/dreamy-volta-pvb4kh`
branch; each commit references its issue (`#N core: ...`).

---

## 1. What the game is (and stays)

**Altered Domination** is a turn-based world-conquest game on a real-world
map. Every country is a player (one human, the rest AI). Cities produce
income, income buys units, units move between linked cities and attack
neighbouring enemy cities. An attack on a defended city opens a **tactical
board battle**: a chess-like grid where every unit type moves and strikes in
its own pattern, both sides promote soldiers to **generals**, and the battle
is won by eliminating every enemy general — a *generalmate*.

Those two layers — the strategic map and the board battle — are the
identity of the game and are kept. Everything around them is rebuilt.

## 2. Audit of the legacy code base

Numbers come from reading every source file in the tree at commit `5af668f`
("sprint 11").

### 2.1 Build, project layout, dependencies

| # | Finding | Evidence | Consequence |
|---|---|---|---|
| A1 | qmake `.pro` project, `QT += widgets quickwidgets svg xml multimedia network`, `CONFIG += resources_big` | `AlteredDomination.pro` | No presets, no tests, no CI; cannot build with Qt 6 (`QtGraphicalEffects 1.0`, `QtQuick.Controls 1.6`, `qrand`, `QSound`, `QGraphicsSvgItem` all removed/changed) |
| A2 | Everything is one flat target: domain, graphics, AI, UI, networking compiled together | `.pro` `SOURCES` list | Game rules cannot be tested without a display; the AI drives the screen |
| A3 | 225 MB of `data/` embedded as resources: 14 MB `mapthem.svg`, three identical 4.4 MB map SVGs, 15 `mapBG*.jpg`, a 2.3 MB `adlogo.bmp`, `data/icons/AD board - Agile Board - AFKAAR_files/` (a saved Jira page), `data/units/junks/`, 100 MB of sounds duplicated as wav + mp3 | `du -sh data/*` | Slow builds, huge binary, unclear provenance |
| A4 | Fonts `Coalition_v2..ttf`, `DESTROY_.ttf`, `District.ttf` and every sound file are of unknown license | `data/fonts`, `data/sounds` | Cannot ship; replace with OFL fonts and audited audio (owner decision, §4) |
| A5 | No license file, no README, no docs, no `.editorconfig`/`.clang-format` | repo root | Nothing tells a contributor how the game or the code works |

### 2.2 Domain model and game logic

| # | Finding | Evidence | Consequence |
|---|---|---|---|
| B1 | Every domain object is a `QObject` with `Q_PROPERTY` boilerplate; containers are copied on every access (`city->units().values().at(i)`) | `domain/*.cpp`, `AI/mapai.cpp:startBattleAI_AI` | Quadratic loops, accidental copies, undefined order |
| B2 | Raw `new` everywhere, mostly parentless; units removed with `deleteLater` while still referenced from lists | `AI/mapai.cpp:310-370`, `graphics/citygraphics.cpp` | Leaks and use-after-free in AI-vs-AI resolution |
| B3 | `Unit::idCount` is a process-wide static; ids restart at 0 on every game and collide after a load | `domain/unit.cpp:4` | Saves cannot be trusted |
| B4 | City coordinates are **mutated on every zoom step** (`city->setX(city()->x()*z)`), so zoom drift accumulates and saves store zoomed pixels | `graphics/citygraphics.cpp:zoomChanged`, `mapview.cpp:zoomIn` | Positions rot over a session; saved games load offset |
| B5 | `qrand()` seeded from the wall clock in three places | `main.cpp`, `mapai.cpp`, `multiplayergame.cpp` | Nothing is reproducible; no way to test the AI |
| B6 | The rules live in the graphics classes: capture in `CityGraphics::startBattle`, turn resolution in `MainMenu::runNextTurn`, win detection in `BFrame::checkWineLose` | `graphics/citygraphics.cpp:230-275`, `mainmenu.cpp:126-165` | No single place states the rules; no tests possible |
| B7 | Battle turn budget: `attackerMoves = 1 + units/8` doubles as *general count* and *moves per turn*; `BFrame::mousePressEvent` is a 300-line phase switch | `domain/battlemap.cpp:19`, `graphics/bframe.cpp:60-330` | Rules and input handling are the same function |
| B8 | Save file written to the **current working directory** as `gamesaves.json` but read as `gameSaves.json`; autosave overwrites slot 0 blindly; no schema version | `mapview.cpp:saveGame/autoSave`, `homemenu.cpp:24` | Saves silently break on case-sensitive file systems |
| B9 | Multiplayer: hard-coded matchmaking port 17777, no server in the repo, blocking `while(!m_closeYet) processEvents()` loops | `homemenu.cpp:entermatchmaking`, `battleform.cpp:onBattleEndedA` | Dead code path; cannot work today |

### 2.3 Data

| # | Finding | Evidence | Consequence |
|---|---|---|---|
| C1 | 972 hand-placed cities with 290 asymmetric neighbour links (A lists B, B does not list A) | `python3` check over `data/map.json` | Movement is one-directional on 15 % of links |
| C2 | Country names with typos (`Andora`, `Croatiia`, `Egypte`, `Nambia`, `Uebekistan`, `Venzuela`, `Ivery Coast`, `Mdagascar`, `Equador`, `Lebannon`) and inconsistent capitalisation (`morocco`, `philippines`) | `data/map.json` | Visible in every screen |
| C3 | City income hand-typed (many countries have every city at 50 or 20), `cityType` inconsistent with income; GDP never actually used | `data/map.json` | "GDP mode" is not GDP-based |
| C4 | Coordinates are in millimetres of a hand-drawn Illustrator map (`x * 2.8346 * zoom`), no projection, no lat/lon | `mapview.cpp:214` | Cannot be regenerated, checked or extended |
| C5 | The map SVG has country paths keyed by ISO code but 9 countries of the JSON have no path and 20 paths have no country | `python3` cross-check of `out.svg` vs `map.json` | Ownership colouring silently skips them |
| C6 | Country ownership colouring is done by **rewriting the SVG on disk** (`applicationDirPath()/out.svg`) and re-parsing the 600 KB file every turn | `graphics/mapgraphics.cpp:reavaluteCountrties` | Slow, fails on read-only installs, whole-country colouring only |

### 2.4 AI

| # | Finding | Evidence | Consequence |
|---|---|---|---|
| D1 | Strategic AI buys from three hard-coded "withdraw vectors" chosen by fund bracket, ignoring what the enemy fields | `AI/mapai.cpp:16-60, 300-330` | Every AI army looks the same |
| D2 | Reinforcement only moves interior units one hop toward "layer 1"; the `layer 2` branch is an empty comment | `AI/mapai.cpp:moveUnitsToFrontLine` | Interior armies never reach the front |
| D3 | Attack decision: any neighbour with less than half our power, chosen with `qrand()` | `AI/mapai.cpp:StudyAttackPossibilities` | No notion of value, risk or garrison |
| D4 | AI-vs-AI battles are resolved by one weighted coin flip and random unit deletion | `AI/mapai.cpp:startBattleAI_AI` | Unit types are irrelevant between AIs |
| D5 | Tactical AI **synthesises `QGraphicsSceneMouseEvent`s** on frames and busy-waits with `processEvents()` for animations | `AI/battleai.cpp:runAnimation`, `applyMoves` | Freezes the UI; untestable; depends on rendering |
| D6 | Move choice is a flat rating list (attack general 9000, escape general 10000, else unit cost); no look-ahead | `AI/battleai.cpp:detect*` | Walks into forks and mates |

### 2.5 Client / UI

| # | Finding | Evidence | Consequence |
|---|---|---|---|
| E1 | Six frameless `QQuickView` windows with `Qt::ToolTip` flags float over a `QGraphicsView`; each popup disables the whole view | `graphics/mapview.cpp:60-140` | Focus/z-order bugs on every desktop, no window-manager integration |
| E2 | QML mixes `QtQuick.Controls 1.6` and `2.0`, `QtGraphicalEffects`, absolute pixel layouts, hex colours in 30 files | `scripts/*.qml`, `CityUI.qml` | Not portable to Qt 6; no theme |
| E3 | Rendering is `QPainter` in `paint()` with `QImage(":/...")` loaded **per paint call** | `graphics/unitgraphics.cpp:paint`, `bframe.cpp:paint` | Constant disk decoding while hovering |
| E4 | City markers are 800×800 items scaled to 0.09 and text drawn at 60 pt | `graphics/citygraphics.cpp` | Blurry at every zoom level |
| E5 | No keyboard navigation, no tooltips, no settings persistence, no high-DPI policy | — | — |
| E6 | 18 PNG frames for the rotating earth, particle "fire" on the home screen; intro cannot be skipped | `scripts/home/*.qml` | Dated, heavy, blocking |

## 3. Target design (summary — the normative docs are separate)

| Doc | Decides |
|---|---|
| [GAME_DESIGN.md](GAME_DESIGN.md) | World, economy, turn order, movement, attacks, capture, victory; the board battle rules |
| [ARCHITECTURE.md](ARCHITECTURE.md) | `core/` vs `client/` layering, determinism, the C++↔QML bridge, threading |
| [AI_DESIGN.md](AI_DESIGN.md) | Strategic AI (threat map, budget, reinforcement, attacks, personalities), tactical AI (alpha-beta search), auto-resolve |
| [UI_THEME.md](UI_THEME.md) | The "command table" look, the `Style` singleton, the from-scratch control style, the map rendering stack |
| [DATA_PIPELINE.md](DATA_PIPELINE.md) | Natural Earth + World Bank sources, city cropping, projection, territories, link graph, flags, icons |

Headline technical choices:

- **Qt 6.8+ (LTS) minimum, tested against 6.11**; QML only (no QtWidgets, no
  QGraphicsView); `qt_add_qml_module` with AOT compilation, `qmllint` in CI.
- **World map as vector geometry**: `QtQuick.Shapes` with the **curve
  renderer** (Qt 6.6+) for country outlines and per-city territories, so the
  map is crisp at any zoom and a conquered city recolours its territory
  with a `ColorAnimation` — no SVG rewriting, no raster. `QtQuick.VectorImage`
  (Qt 6.8+) draws the SVG unit icons and flags on the scene graph;
  `QtQuick.Effects` (`MultiEffect`) replaces `QtGraphicalEffects`; the ocean
  is a `ShaderEffect` compiled with `qt_add_shaders`.
- **Real data**: Natural Earth 1:50m countries, 1:10m populated places,
  World Bank GDP — reproducible from one script, cropped to ~1000 cities.
- **Deterministic core**: seeded RNG, integer economy, resumable AI round so
  the client can play the human's battles in the middle of it.
- **Search-based tactical AI** in core, running on a worker thread in the
  client, node-limited (not time-limited) so a seed replays identically.

## 4. Milestones and issues

Order is dependency order; every milestone ends with green tests and, for
client work, a screenshot in the issue.

GitHub Issues are disabled on the repository (only the owner can enable
them), so the tickets are kept filing-ready in [docs/issues/](issues/README.md)
with the numbers below; they are filed verbatim the day Issues are switched
on, and each commit references its number as `#N area: subject`.

| Milestone | Issues | Delivers |
|---|---|---|
| M0 Skeleton | 01 | CMake + presets + CPM, `core/` + doctest, `client/` stub, CI, docs, CLAUDE.md, legacy tree removed |
| M1 Data | 02, 03, 04 | `scripts/gen_world_data.py`, `assets/world/*.json`, flags, unit icons |
| M2 Core campaign | 05, 06, 07 | World model, campaign rules, save/load |
| M3 Core battle | 08 | Board battle engine |
| M4 AI | 09, 10, 11 | Strategic AI, tactical AI, auto-resolve |
| M5 Client shell | 12, 13 | Theme + controls, Home/New game/Load/Settings/Codex, bridge |
| M6 Map | 14, 15 | World map view, campaign HUD |
| M7 Battle | 16 | Battle screen |
| M8 Polish | 17, 18, 19 | Audio, saves UI, keyboard/tooltips/headless driver |
| Owner | 20, 21, 22 | License, asset provenance, online multiplayer decision |

## 5. Out of scope for this pass

- Online multiplayer and a matchmaking server (the legacy TCP code is
  removed; a hotseat mode replaces it for two humans on one machine). An
  epic issue records the decision the owner has to make.
- Android / WebAssembly builds — the owner asked for desktop only.
- Diplomacy, fog of war, supply lines — listed as future design issues.
