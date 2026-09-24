# Altered Domination — Architecture

```
AlteredDomination/
├── core/          ad-core: deterministic game logic (C++23, nlohmann_json, no Qt)
│   ├── include/ad/core/   public headers (the contract)
│   ├── src/               world loading, campaign, battle, AI, save
│   └── tests/             doctest unit tests (test_*.cpp, auto-globbed)
├── client/        ad: Qt Quick desktop app
│   ├── src/               main.cpp + C++↔QML bridge (controllers/models)
│   ├── qml/               pure QML UI (qt_add_qml_module), styles/, components/
│   ├── shaders/           GLSL for qt_add_shaders (ocean, paper)
│   └── fonts/             bundled OFL fonts
├── assets/        generated + curated data (world, units, flags, icons, audio)
├── scripts/       data pipeline, icon generator, the `ad` dev CLI
├── docs/          design docs (normative)
├── cmake/         CPM bootstrap + dependencies
└── .claude/       skills used to develop the game (screenshots, tickets)
```

## Layering rules

- `core/` depends only on **nlohmann_json** (to read the generated data and
  the saves). It never includes Qt and never touches the file system except
  through the byte strings the client hands it — the client owns paths.
- `client/` depends on `ad::core` + Qt Quick. All UI is **pure QML**; C++ is
  the bridge: models feed data in, controllers accept commands.
- Tests cover `core/` exhaustively (every rule of GAME_DESIGN.md and every
  AI decision of AI_DESIGN.md). Bridge code that is testable without a
  display gets doctest coverage in `client/tests/`.

## Determinism

- The campaign carries one **seeded RNG** (`ad::core::Rng`, a 64-bit
  SplitMix/PCG, serialised in the save). Nothing in `core/` reads the clock.
- The board battle uses **no randomness** at all; its AI is node-limited.
- Every container that is iterated for game decisions is ordered: cities by
  id, units by id, countries by key. `Campaign::stateHash()` and
  `Battle::stateHash()` hash the canonical state and are asserted in the
  tests (a replay of the same commands on the same seed is bit-identical).

## The core API (headers)

| Header | Contents |
|---|---|
| `constants.hpp` | every tunable number of GAME_DESIGN.md §11 |
| `rng.hpp` | `Rng` — seedable, serialisable |
| `catalog.hpp` | `UnitType`, `UnitClass`, pattern tables; `loadCatalog(json)` |
| `world.hpp` | `World` — countries, cities, links, territories (immutable data) |
| `campaign.hpp` | `Campaign` — the mutable game state + `apply(Command)`, `stateHash()`, `snapshot views` |
| `commands.hpp` | `Recruit`, `Move`, `Attack`, `EndTurn` (+ result enum) |
| `battle.hpp` | `Battle` — board state, `apply(BattleCommand)`, legal-move queries, result |
| `ai/strategic.hpp` | `StrategicAi` — one AI player's turn as a list of commands + the resumable `AiRound` |
| `ai/tactical.hpp` | `TacticalAi` — formation, promotion, `bestAction(battle, budget)` |
| `save.hpp` | `toJson(Campaign)`, `fromJson(json, World)` |

`AiRound` is a **resumable** object: `step()` applies one AI command and
returns an `AiProgress` event (`TurnStarted`, `Recruited`, `Moved`,
`Captured`, `BattleResolved`, `BattleNeedsHuman`, `TurnEnded`,
`RoundFinished`). When it needs a human battle it stops until
`resumeWithBattleResult(result)` is called. The client steps it in time
slices from the main thread's event loop and plays the human's battles in
between; the tests run it to completion with the tactical AI on both sides.

## C++ ↔ QML bridge (client)

Performance rule: models are updated by **deltas** from core events, never
rebuilt per action. During the AI round the deltas are batched: cities
touched in a time slice are refreshed once when the slice ends, the link
paths are rebuilt at most once per slice, the ranking once per slice.

- `GameController` (QML singleton): owns the `Campaign`, starts/loads/saves
  games, exposes `Q_INVOKABLE recruit/moveUnits/attack/endTurn` and the
  `selectedCity`/`interaction` properties, steps the AI round from a
  zero-interval `QTimer` (at most ~8 ms per tick, so the UI keeps
  animating) and surfaces `battleRequested`, `aiStatus`/`aiProgress`,
  `notice`, `cityCaptured`, `roundEnded`, `gameEnded`.
- `WorldModel` (`QAbstractListModel`, one row per city): id, key, name,
  x, y, tier, isCapital, ownerKey, ownerColor, originalKey, unitCount,
  power, territoryPath (SVG path string), selected/highlight flags,
  reachable-from-selection flags (move / attack targets).
- `LinkModel`: one row per drawn link segment (x1, y1, x2, y2, sea, wrap,
  hostile, active; an antimeridian link has two rows) plus the links as
  SVG path strings per kind (`landPath`, `seaPath`, `hostilePath`,
  `activePath`), so the map draws each kind with one `Shape`.
- `CountryModel` (ranking): key, name, color, flag, income, funds, cities,
  share, eliminated, isHuman.
- `CityUnitsModel`: the selected city's units grouped by type.
- `CatalogModel`: the unit types (cost, classes, icon, patterns for the
  codex diagram).
- `BattleController` (`GameController.battle`): owns the current `Battle`,
  exposes `BoardModel` (one row per unit: cell, side, general, acted,
  alive), phase, side to act, actions left, the selection's legal targets,
  `Q_INVOKABLE` commands, and runs the tactical AI on a worker
  (`QtConcurrent::run` over a private copy of the battle) — the UI never
  blocks, and every AI action is animated before the next is asked for.
- `SaveStore`: slots + metadata list model in `AppDataLocation`.
- `Style` (singleton, UI_THEME.md), `Audio` (singleton: `SoundEffect` pool +
  `MediaPlayer` music), `DevDrive` (headless screenshot/driver armed by
  `AD_DRIVE=1`; input goes in through `QWindowSystemInterface`, the door
  real mice and keyboards use, so popups and flickables see it; see the
  share-screenshot skill and `scripts/ad.py`).

## Threading

Only the tactical AI leaves the main thread: `bestAction()` and the
auto-resolve `playOut()` run through `QtConcurrent` on a **copy** of the
battle, and the result is applied on the main thread when the
`QFutureWatcher` finishes (a generation counter drops results of a battle
that was quit meanwhile). The strategic AI round never leaves the main
thread: `AiRound::step()` is cheap (a whole 206-player round is a few
hundred milliseconds in a release build), so the controller steps it in
~8 ms slices from a zero-interval `QTimer` and the models are updated by
deltas between slices. No second thread ever touches the campaign.

## Build

CMake ≥ 3.27, Ninja, C++23 (GCC 13+, Clang 17+, MSVC 19.38+). Presets:
`core-debug`, `core-asan`, `desktop-debug`, `desktop-release`. Dependencies
(nlohmann_json, doctest) are fetched by CPM into `~/.cache/cpm`; Qt 6.8+
(Quick, QuickControls2, Shapes, Effects, VectorImage, Svg, Multimedia,
ShaderTools) is found via `CMAKE_PREFIX_PATH`.
