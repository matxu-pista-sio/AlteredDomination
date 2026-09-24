# Altered Domination — AI Design

Two AIs, both in `core/`, both deterministic, both playing through the same
command API as a human — no hidden information, no cheats beyond the
difficulty income scale (GAME_DESIGN.md §6).

| AI | Where | Decides |
|---|---|---|
| **Strategic** (`ai/strategic.hpp`) | one call per AI player per round | what to recruit, where to move, whom to attack |
| **Tactical** (`ai/tactical.hpp`) | during a board battle | formation, generals, every action |

This document is normative for `core/src/ai/*.cpp`; `core/tests/test_ai_*.cpp`
asserts every decision rule below.

---

## 1. Strategic AI — the turn

`StrategicAi::playTurn(campaign, player) → std::vector<Command>` runs five
steps in order and returns the commands it would issue; `AiRound` applies
them and handles the battles they trigger.

### 1.1 Analysis (one pass, cached for the turn)

For every city the AI owns:

| Field | Meaning |
|---|---|
| `power` | Σ cost of units in the city |
| `enemyPressure` | Σ over linked cities owned by *other* players of their `power` (a neutral/other AI counts the same as the human — every country is an opponent) |
| `frontier` | true when at least one linked city is foreign |
| `distance` | BFS hops to the nearest frontier city over *own* cities (0 for frontier cities; `∞` when no frontier is reachable) |
| `targets` | the foreign linked cities, with their `power`, `income` and `isCapital` |

Global: `funds`, `income`, `personality`, the per-type **counter table**
§1.3.

### 1.2 Recruitment

Budget `spend = floor(funds · spendRatio)` (personality, §2), spent city by
city:

1. **Defence first**: frontier cities in descending `enemyPressure − power`;
   each gets `min(spend, enemyPressure · defenceMargin − power)` when that is
   positive (`defenceMargin` per personality).
2. **Then expansion**: what remains goes to the frontier city with the best
   attack prospect (§1.4 score) so it can reach `attackMargin` over its best
   target; the remainder, if any, to the capital.
3. **Composition**: a city's allowance is spent in rounds: the counter
   table §1.3 is consulted against the classes seen in its `targets`, the
   most-wanted affordable type is bought, then the next, until nothing
   affordable is wanted; soldiers fill the last coins. Every purchase is
   validated exactly as `Campaign::apply` would (funds, ownership).

### 1.3 Counter table

Which unit types are *wanted* given what the enemy fields around a city:

| Enemy fields | Wanted (in order) |
|---|---|
| `air` units and we have no `antiaircraft` | antiaircraft, fighter |
| mostly `machine` units | rocketlauncher, tank, mlrs, modernarmor |
| mostly `human` units | afv, sniper, attackhelicopter, soldier |
| nothing yet | soldier, afv, tank |

"Mostly" is the majority of the enemy's unit **count** in the linked foreign
cities; ties read as machine. The table is data in `strategic.cpp` and the
tests assert the pick for each row.

### 1.4 Attack decisions

For every frontier city with unacted units and every target:

```
winEstimate = ownPower / (ownPower + targetPower · defenceBias)   // 0..1
value       = targetIncome + (target.isCapital ? kCapitalBonus : 0)
score       = winEstimate · value
```

`defenceBias = 1.15` (the board favours the side that acts second only
slightly; the defender fields everything it has). An attack is issued when
`winEstimate ≥ attackThreshold` (personality) **and** the city would keep
at least `garrisonMin` units of power `≥ garrisonPower` behind. An
**undefended** target (`targetPower == 0`) is always taken with the
cheapest single unit that has not acted — free real estate.

At most `maxAttacks` per turn (personality), highest `score` first; a city
attacks once per turn. The attacking force is *every* unacted unit in the
city minus the garrison it keeps.

### 1.5 Reinforcement

After attacks are decided (so units that attack are excluded), every
interior city (`distance ≥ 1`) sends all but `garrisonMin` of its unacted
units one hop along a shortest path to the nearest frontier city; ties
broken by lowest destination city id. Frontier cities never send units
away. This is the flow that the legacy AI's "layer 2" never implemented:
armies drain toward the front every round.

### 1.6 Order of the five steps

Recruit → decide attacks (marks the attackers) → reinforce → issue attacks
→ end turn. Recruiting before attacking means fresh units join the assault
(GAME_DESIGN.md §4 lets them act); reinforcing before the attacks are issued
lets interior units arrive a hop closer, not join this turn's fights.

## 2. Personalities

Drawn from the campaign seed and the country key (`hash(seed, key) mod 4`),
so a given seed always gives the same map of temperaments:

| Personality | spendRatio | defenceMargin | attackThreshold | maxAttacks | garrisonMin |
|---|---|---|---|---|---|
| **Expansionist** | 0.90 | 1.0 | 0.55 | 3 | 1 |
| **Balanced** | 0.75 | 1.2 | 0.62 | 2 | 1 |
| **Defensive** | 0.65 | 1.5 | 0.75 | 1 | 2 |
| **Opportunist** | 0.80 | 1.0 | 0.50 (only vs targets with `targetPower ≤ 0.5 · ownPower`) | 2 | 1 |

`garrisonPower` is `5 · garrisonMin` (a soldier per unit of garrison).

## 3. AI-vs-AI battles — auto-resolve

A battle between two AI players is played out by the **tactical AI on both
sides** (§4) at the `Quick` budget, with the same engine and rules as a
human battle, capped by the normal turn rules. The result is real: unit
types, formations and generals matter between AIs exactly as they do
against the player. The human can pick "auto-resolve" for their own
battles too (a setup option, off by default) — it runs the same thing at
the `Normal` budget for their side.

Performance budget: one full AI round on the generated world (≈ 180 AI
players) must complete in **under 2 s** in a release build on a 2020
laptop core, tested with a fixed seed (`test_ai_round.cpp` asserts an
upper bound of node counts, not wall time).

## 4. Tactical AI

### 4.1 Formation

The engine formation (GAME_DESIGN.md §8.3) is the AI's formation; it never
rearranges. (A later issue may teach it to shield generals; the tests pin
today's behaviour.)

### 4.2 Promotion

`g` generals are promoted in this order: soldiers in the back column,
centre rows first; then soldiers in the middle column; then soldiers in the
front; then — only when soldiers run out — the cheapest non-soldiers (which
the rule converts). The intent is obvious: generals stand at the back,
behind everything, in the middle.

### 4.3 Search

`bestAction(battle, side, budget) → BattleCommand` is a **negamax
alpha-beta** search over the battle state:

- A ply is one action (`MoveUnit`, `Strike` or `EndTurn`); the side to move
  changes when its `actionsLeft` hits 0 or it ends the turn, exactly as the
  engine does — the search literally calls `Battle::apply` on a copy.
- **Move ordering**: strikes on generals, then strikes by target cost
  descending, then moves that threaten a general, then moves toward the
  nearest enemy general (Manhattan), then the rest; `EndTurn` last.
- **Iterative deepening** from depth 1 up, stopping when the **node budget**
  is spent (§4.5). The best move of the last *completed* depth is returned,
  so the answer never depends on wall time.
- **Evaluation** (from the side to move's view, in cost units):

  ```
  material   = Σ cost(own) − Σ cost(enemy)
  generals   = kGeneralValue · (ownGenerals − enemyGenerals)      // 400 each
  threats    = − kThreat · (own generals strikeable next turn)     // 150 each
             + kThreat · (enemy generals strikeable now)
  advance    = kAdvance · (Σ enemy non-generals' distance to my nearest general
                 − Σ own non-generals' distance to the nearest enemy general) // 1 each
             // zero-sum, so eval(pov) == −eval(other) and negamax stays honest
  ```

  (A mobility term was tried and dropped: counting every legal action of
  both sides per node cost more than the depth it bought.)

  Terminal states score `±kMate` (100 000) adjusted by depth so faster
  mates win.
- **Transposition table** keyed by `Battle::stateHash()`, sized 2^16
  entries, cleared per `bestAction`.

### 4.4 Draw and surrender

- Offers a draw when its material is at most `0.6·` the enemy's and the
  battle has run ≥ 40 turns; accepts an offered draw under the same
  condition.
- Surrenders when it has no unit that can affect any enemy class present
  (e.g. only anti-aircraft left against pure infantry) **and** its material
  is under a tenth of the enemy's.

### 4.5 Budgets

| Budget | Max nodes per action | Used by |
|---|---|---|
| `Quick` | greedy: the best of the first 8 ordered actions by static evaluation, no look-ahead | AI-vs-AI auto-resolve (hundreds of battles a round) |
| `Easy` | 3 000 | difficulty Easy vs the human |
| `Normal` | 25 000 | difficulty Normal, the human's own auto-resolve |
| `Hard` | 150 000 | difficulty Hard |

Node counts, not milliseconds, so a seed replays identically on any
machine.

## 5. Determinism and tie-breaking

No RNG anywhere in either AI beyond the campaign `Rng` used **once** to
shuffle the AI turn order at campaign start. Every scan is ordered (cities
by id, units by id, targets by id, actions by the ordering of §4.3);
identical state produces identical commands (`ai: identical state produces
identical command lists`).
