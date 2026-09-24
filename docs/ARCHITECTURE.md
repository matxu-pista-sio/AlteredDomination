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

`AiRound` is a **resumable** object: `step()` advances the AI turns and
returns `Progress` events (`AiTurnStarted`, `Recruited`, `Moved`,
`CaptureUndefended`, `BattleAiVsAi{result}`, `BattleNeedsHuman{attacker,
defender, unitIds}`, `RoundFinished`). When it needs a human battle it stops
until `resumeWithBattleResult(result)` is called. The client runs `step()`
on a worker thread and plays the human's battles on the main thread; the
tests run it to completion with the tactical AI on both sides.

## C++ ↔ QML bridge (client)

Performance rule: models are updated by **deltas** from core events, never
rebuilt per action.

- `GameController` (QML singleton): owns the `Campaign`, starts/loads/saves
  games, exposes `Q_INVOKABLE recruit/move/attack/endTurn/selectCity`, runs
  the AI round on a `QThread` worker and surfaces `battleRequested`,
  `aiProgress`, `roundEnded`, `gameOver`.
- `WorldModel` (`QAbstractListModel`, one row per city): id, key, name,
  x, y, tier, isCapital, ownerKey, ownerColor, originalKey, unitCount,
  power, territoryPath (SVG path string), selected/highlight flags,
  reachable-from-selection flags (move / attack targets).
- `LinkModel`: one row per link (x1, y1, x2, y2, sea, hostile).
- `CountryModel` (ranking): key, name, color, flag, income, funds, cities,
  share, eliminated, isHuman.
- `CityUnitsModel`: the selected city's units grouped by type.
- `CatalogModel`: the unit types (cost, classes, icon, patterns for the
  codex diagram).
- `BattleController` (QML singleton): owns the current `Battle`, exposes
  `BoardModel` (cells with unit, general, highlight roles), phase, side to
  act, actions left, `Q_INVOKABLE` commands, and runs the tactical AI on a
  worker (`QtConcurrent::run`) — the UI never blocks, and every AI action
  is animated before the next is asked for.
- `SaveStore`: slots + metadata list model in `AppDataLocation`.
- `Style` (singleton, UI_THEME.md), `Audio` (singleton: `SoundEffect` pool +
  `MediaPlayer` music), `DevDrive` (headless screenshot/driver, see the
  share-screenshot skill).

## Threading

Only two places leave the main thread: `AiRound::step()` and the tactical
AI's `bestAction()`. Both operate on state the UI does not touch while they
run (the campaign during the AI round is read-only for the models until
`roundEnded`; the battle board is locked while the AI thinks). Results come
back as queued signals.

## Build

CMake ≥ 3.27, Ninja, C++23 (GCC 13+, Clang 17+, MSVC 19.38+). Presets:
`core-debug`, `core-asan`, `desktop-debug`, `desktop-release`. Dependencies
(nlohmann_json, doctest) are fetched by CPM into `~/.cache/cpm`; Qt 6.8+
(Quick, QuickControls2, Shapes, Effects, VectorImage, Svg, Multimedia,
ShaderTools) is found via `CMAKE_PREFIX_PATH`.
