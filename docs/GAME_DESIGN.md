# Altered Domination — Game Design Document

Altered Domination is a turn-based world-conquest game on a real-world map
with chess-like tactical board battles. This document is **normative**: the
core simulation (`core/`) implements exactly these rules, every rule has a
doctest case in `core/tests/`, and a rule change updates both the code and
this file. All tunable numbers live in `core/include/ad/core/constants.hpp`
(the world data itself is generated, see DATA_PIPELINE.md).

---

## 1. The world

- The world is a graph of **cities** grouped into **countries**, drawn on a
  Miller-cylindrical projection of the Earth (DATA_PIPELINE.md §3).
- A **country** has a stable ISO 3166-1 alpha-2 key (`"fr"`), a display
  name, a banner colour, a flag, a GDP figure (USD), a population, a
  continent and a **capital** city. Every country is a **player** from turn
  one: exactly one is the human, the others are AI (§6). Two humans on one
  machine is *hotseat* (§7).
- A **city** has a stable id (dense, `0..N-1`, assigned by the generator),
  a key (`"fr-paris"`), a name, its original country, lon/lat and projected
  map coordinates, a population, a **tier** and a **territory** polygon
  (the part of its country closest to it). Tiers by population:

  | Tier | Population | Marker |
  |---|---|---|
  | 0 town | < 300 k | small ring |
  | 1 city | 300 k – 1 M | ring |
  | 2 metropolis | 1 M – 5 M | ring + inner dot |
  | 3 megacity | ≥ 5 M | double ring |

- **Links** join pairs of cities. A link is undirected, either *land* or
  *sea* (drawn dashed), and is the only way units travel. Every city has at
  least one link and the link graph is connected (DATA_PIPELINE.md §5), so
  every city can be reached from every other.
- **Ownership** is per city. A country's *territory on the map* is the union
  of the territories of the cities it currently owns — conquering a city
  recolours exactly that city's territory.

## 2. Units

Eleven unit types, defined in `assets/units/units.json` and loaded by the
core catalog. Each has a cost, a set of **classes** and two **pattern
tables** (§8.4):

| Key | Name | Cost | Classes |
|---|---|---|---|
| `soldier` | Soldier | 5 | human, land |
| `sniper` | Sniper | 10 | human, land |
| `rocketlauncher` | Rocket Launcher | 15 | human, land |
| `artillery` | Artillery | 20 | human, land |
| `afv` | Armoured Fighting Vehicle | 30 | machine, land |
| `tank` | Tank | 50 | machine, land |
| `antiaircraft` | Anti-Aircraft | 60 | machine, land |
| `attackhelicopter` | Attack Helicopter | 70 | machine, air |
| `mlrs` | MLRS | 90 | machine, land |
| `modernarmor` | Modern Armour | 100 | machine, land |
| `fighter` | Fighter | 120 | machine, air |

- The **power** of a group of units is the sum of their costs. Power is the
  strategic layer's only notion of strength; the board battle is what
  decides fights.
- A unit belongs to exactly one city at any time (or to a battle while one
  runs). Units have a dense per-campaign id; ids are never reused within a
  campaign and are part of the save.

## 3. Economy

- Every city has an **income** per round. Two modes, chosen at setup:
  - **GDP mode** (default): the country's round budget is
    `B = round(kGdpScale · (gdp / 1e12) ^ kGdpExponent)`, floored at
    `kMinCountryBudget` (`kGdpScale = 1200`, `kGdpExponent = 0.55`,
    `kMinCountryBudget = 12`). The exponent compresses the range so that a
    small economy still buys a unit a turn while the United States
    (≈ 6 500) outproduces Andorra (≈ 50) by two orders of magnitude, not
    four. A city's share of `B` is `w_i / Σ w` with
    `w_i = population_i ^ 0.75`, times `1.25` for the capital; every city
    earns at least `kMinCityIncome = 2`.
  - **Equality mode**: every city earns `kEqualIncome = 60`.
- **Funds** are per country, integer, never negative. A country starts with
  `kStartingRounds = 3` rounds of income banked. From the second round on,
  income is paid to every surviving country at the **start of the round**
  (§4), then the round's turns follow.
- Income belongs to whoever owns the city at the moment it is paid.
- **Home guard**: every city starts with `kHomeGuardBase + tier` free
  soldiers (`2`, so 2–5), capitals with `kHomeGuardCapitalBonus = 2` more.
  Nothing is undefended on round one, so the first conquests are fought on
  the board rather than walked into.
- **Capital loot**: when a country loses **its own** capital (the city the
  world data marks as such, taken from the country it belongs to),
  `kCapitalLoot = 50 %` of that country's funds transfers to the conqueror
  (rounded down). Taking the same city from a later occupier loots nothing.
  The city stays that country's capital marker for the rest of the game.

## 4. Rounds and turns

A **round** is: income → the human player's turn → every AI player's turn
in a fixed seeded order → end-of-round bookkeeping → autosave. The round
counter starts at 1.

- During a turn the acting player may perform any number of **actions** in
  any order: recruit (§5.1), move (§5.2), attack (§5.3). The turn ends when
  the player ends it (the AI ends its own).
- Every unit carries an `acted` flag, cleared for all units of a player at
  the start of that player's turn. Moving or attacking sets it; a unit acts
  **once per round**. Recruited units arrive with `acted = false`, so they
  can defend, move or attack the same turn (tempo is a feature).
- A country whose last city falls is **eliminated at once**: its player is
  skipped from then on and its remaining funds vanish. When the last human
  is eliminated the game is over (§9).
- End of round: victory (§9) is checked; the round counter increments;
  income is paid.

## 5. Actions

### 5.1 Recruit

`Recruit{player, cityId, unitType, count}` — valid when the player owns the
city, `count ≥ 1` and `count · cost ≤ funds`. Units appear in the city at
once. There is no garrison cap and no upkeep.

### 5.2 Move

`Move{player, fromCity, toCity, unitIds}` — valid when the player owns both
cities, they are linked, every listed unit is in `fromCity` and has not
acted. The units change city and are marked acted. Sea links cost nothing
extra: transport is abstracted away.

### 5.3 Attack

`Attack{player, fromCity, toCity, unitIds}` — valid when the player owns
`fromCity`, does **not** own `toCity`, the cities are linked, `unitIds` is
non-empty, every listed unit is in `fromCity` and has not acted. The listed
units are marked acted whatever happens.

- **Undefended target** (no units): the city is captured immediately, the
  attacking units move in.
- **Defended target**: a board battle (§8) starts between the attacking
  units and *every* unit in the target (a defender defends with everything
  it has; units that already acted still fight). Its outcome:
  - *attacker wins* — the defender's remaining units are destroyed, the
    surviving attackers move into the city, the city changes owner;
  - *defender wins* — the surviving attackers return to `fromCity`;
  - *draw* — both sides keep their survivors where they were.
- **Battle cap**: at most `kBattleSideCap = 48` units per side take part.
  An attack listing more than `48` units is refused (`ErrForceTooLarge`):
  the attacking player chooses the force, so the cap only ever trims a
  defender. When the defender has more, the `48` most expensive fight and
  the rest sit out; if the city falls, the units that sat out die with it,
  otherwise they are untouched.
- Capture never destroys the captured city's buildings — there are none;
  only ownership and units change.

## 6. Players

- Type `Human` or `Ai`. AI players get a **personality** (AI_DESIGN.md §2)
  drawn deterministically from the campaign seed and their country key.
- **Difficulty** (setup): `Easy`, `Normal`, `Hard`. It scales every AI
  country's income by `0.8 / 1.0 / 1.25` and sets the tactical search
  budget (AI_DESIGN.md §4.5). The human is never scaled.
- Turn order within a round: the human first, then AI players sorted by
  country key, shuffled once per campaign with the seed.

## 7. Hotseat

Setup may mark more than one country as human. The round then contains one
turn per human, in setup order, before the AI turns; a battle between two
humans is played on the board by both, alternating at the keyboard. Nothing
else changes. (Online multiplayer is deliberately not part of this design;
see the owner decision issue.)

## 8. Board battle

A battle is a self-contained, deterministic mini-game on a grid. It has
its own state, its own commands and its own tests; the campaign only reads
its result.

### 8.1 Geometry

- The board is `kBoardLength = 14` cells long (the `x` axis, attacker on
  the left, defender on the right) and `w` cells wide (the `y` axis), where
  `w = clamp(kBoardMinWidth = 5, 2 + ceil(max(nA, nD) / 3), kBoardMaxWidth
  = 16)` and `nA`, `nD` are the two sides' unit counts.
- The **deployment zones** are the attacker's three left columns
  (`x ∈ [0, 2]`) and the defender's three right columns (`x ∈ [11, 13]`).
  `3 · w ≥ n` always holds for both sides, so every unit fits.
- Cells are addressed `(x, y)`, `0 ≤ x < 14`, `0 ≤ y < w`.

### 8.2 Phases

`Deploy → Promote → Play → Over`.

1. **Deploy**: each side's units are placed in its zone by the engine
   (§8.3) and each side may then swap any two of its own cells inside its
   zone, or move a unit to an empty cell of its zone, as often as it likes
   (`Rearrange{side, from, to}`). `Ready{side}` locks the side; both ready
   → Promote. The AI readies immediately with its formation.
2. **Promote**: each side must promote exactly `g = 1 + floor(n / 8)`
   **generals** (`n` = its units on the board), via `Promote{side, cell}` /
   `Demote{side, cell}`. Only a `soldier` can be a general; promoting any
   other unit **converts it into a soldier** first (the historical rule:
   generals lead from a jeep, not a tank). `Ready{side}` with exactly `g`
   generals locks the side; both ready → Play. The attacker acts first.
3. **Play**: sides alternate **turns**. A turn grants the acting side
   `actionsLeft = living generals` actions; an action is `MoveUnit{side,
   from, to}` or `Strike{side, from, target}`; `EndTurn{side}` forfeits
   the rest. A unit acts at most once per turn. A side with no legal action
   passes automatically.
4. **Over**: reached by generalmate, surrender, draw or the turn caps.

### 8.3 Engine deployment

The engine's initial formation (used for both sides, then editable by a
human): the column nearest the enemy holds the `human`-class units, the
middle column the `machine, land` units, the back column the `air` units
and any overflow; inside a column units fill from the centre row outward
(`w/2, w/2−1, w/2+1, …`), most expensive first. Overflow fills the next
column back. This is also the AI's formation (AI_DESIGN.md §4.1).

### 8.4 Patterns

Each unit type lists **moves** and **strikes** as relative offsets
`(dx, dy)` from its cell, with `dx` pointing toward the enemy for the
attacker. For the **defender every `dx` is mirrored** (`dx → −dx`), so the
same table reads "forward" for both sides; `dy` is never mirrored.

- A **move** `{dx, dy, path}` is legal when the target cell is on the board
  and empty and every cell in `path` (offsets, in order) is empty. Moves
  do not depend on what stands on the target: an occupied cell is never a
  move.
- A **strike** `{dx, dy, path, affects}` is legal when the target cell
  holds an **enemy** unit that has at least one class in `affects`, and
  every cell in `path` is empty. A strike destroys the target outright;
  the striker **stays** where it is (the board has ranged, not capturing,
  attacks). Friendly units are never valid targets.
- The tables are data (`assets/units/units.json`, DATA_PIPELINE.md §7);
  the tests pin the shape of every type: e.g. a soldier moves one
  orthogonally (two with a clear first step) and strikes knights-move
  cells against humans; artillery strikes four cells straight against any
  land unit; a fighter moves up to five in any straight or diagonal line
  and strikes the eight neighbours and the four two-away orthogonals
  against land and air.

### 8.5 Generals and the end

- Destroying a general reduces the victim's *living generals*, hence its
  actions per turn from the next turn on. If it was destroyed during the
  victim's own turn (impossible — only the acting side strikes) nothing
  special happens.
- **Generalmate**: a side with **zero living generals** loses at once.
- **Surrender** `{side}` ends the battle as a loss for that side, any
  time after Play starts.
- **Draw**: `OfferDraw{side}` stands until the other side offers too (draw)
  or the offering side acts (withdrawn). The battle is also drawn when the
  **turn cap** `kBattleTurnCap = 80` turns is reached, or after
  `kBattleQuietTurns = 30` consecutive turns without a strike.
- Result: `AttackerWins | DefenderWins | Draw`, plus the surviving units
  of both sides. Generals stay soldiers after the battle (the conversion
  is permanent); the general flag itself is dropped.

### 8.6 Determinism

The battle uses no randomness. Given the same two unit lists (in id order)
and the same command sequence, the state is identical; `Battle::stateHash()`
pins that in the tests. AI decisions (AI_DESIGN.md §4) are node-limited,
never time-limited, for the same reason.

## 9. Victory and defeat

- A human player **loses** when eliminated (§4); when every human is
  eliminated the campaign ends at once with no winner.
- A human player **wins** when, at the end of a round, they own cities whose
  income sums to at least `kDominationShare = 60 %` of the world's total
  income, or every city. In hotseat the first human to reach it wins.
- The campaign then enters `GameOver`; the map stays browsable, no further
  actions validate.
- The **ranking** panel orders surviving countries by income, then funds,
  then city count.

## 10. Saves

- A save is one JSON document, schema `"version": 1`: campaign settings
  (seed, mode, difficulty, human countries), the round counter, whose turn
  it is, every country's owner-independent data reference (by key), funds,
  eliminated flag, every city's owner and unit list (id, type, acted), and
  the RNG state. World data is not saved — it is referenced by the world
  file's content hash, so a save refuses to load against a different world.
- Saves live in `QStandardPaths::AppDataLocation/saves/<slot>.json` with a
  sidecar of metadata (country, round, date, income share) for the list.
  `autosave` is a slot like any other, written at the end of every round.
- A battle in progress is never saved. Quitting a battle before Play
  (during Deploy or Promote) withdraws the attackers with no losses;
  quitting during Play concedes it as it stands — a surrender by the
  quitting side — which is what the "quit battle" confirmation says.

## 11. Constants

| Name | Value | Where used |
|---|---|---|
| `kGdpScale` / `kGdpExponent` | 1200 / 0.55 | §3 |
| `kMinCountryBudget` / `kMinCityIncome` | 12 / 2 | §3 |
| `kEqualIncome` | 60 | §3 |
| `kStartingRounds` | 3 | §3 |
| `kHomeGuardBase` / `kHomeGuardCapitalBonus` | 2 / 2 | §3 |
| `kCapitalLootPercent` | 50 | §3 |
| `kBattleSideCap` | 48 | §5.3 |
| `kBoardLength` | 14 | §8.1 |
| `kBoardMinWidth` / `kBoardMaxWidth` | 5 / 16 | §8.1 |
| `kDeployColumns` | 3 | §8.1 |
| `kUnitsPerGeneral` | 8 | §8.2 |
| `kBattleTurnCap` / `kBattleQuietTurns` | 80 / 30 | §8.5 |
| `kDominationShare` | 60 % | §9 |
| difficulty income scale | 0.8 / 1.0 / 1.25 | §6 |
