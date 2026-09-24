# Altered Domination — Network Protocol (v1)

Transport: WebSocket, JSON text frames. The server (`ad-server`,
`server/`) handles **accounts, stats and ELO, matchmaking, the match
setup, and the relay of the match's command log**. It never simulates a
campaign: both clients run the deterministic core (docs/ARCHITECTURE.md
"Determinism") on the same seed and apply the same commands in the same
order — **peer lockstep** — and the server checks that they agree by
comparing the state hashes they report at every round end.

Why lockstep and not a server-hosted simulation (TopGen's v2): a campaign's
state is 987 cities of owners and units, far too much to stream after every
recruit, while a command is a few dozen bytes; the core's `stateHash()` is
pinned by the test suite precisely so that two machines replaying one log
reach one state. A server-hosted campaign stays possible later — the relay
already sees every command — but is not this version.

Every frame: `{"t": "<type>", ...fields}`. Server responses to a request
carry the same `"t"` with `_ok` / `_err` suffix. Errors:
`{"t":"<type>_err","reason":"..."}`; an unrecognised `"t"` answers
`frame_err`; unparsable JSON answers `{"t":"error","reason":"bad_frame"}`.

## 1. Session

| Client → Server | Server → Client |
|---|---|
| `{"t":"hello","proto":1,"name":"Alice","token":"<uuid-or-empty>","platform":"linux","app_version":"0.1.0"}` | `{"t":"hello_ok","player_id":123,"token":"<uuid>","name":"Alice"}` |

- First connect with an empty token registers a new player; the returned
  token is stored client-side (`QSettings`, keyed by server) and re-used to
  authenticate. There are no passwords in v1: a token is the identity.
- `proto` mismatch → `hello_err` reason `unsupported_proto`; an unknown
  token → `unknown_token` (the client clears it and registers again).
- A name collision gets a `#2` … `#6` suffix, then a random 4-hex suffix.
- One connection per player, newest wins: authenticating a token that is
  already online displaces the older session, which is told
  `{"t":"session_end","reason":"displaced"}` before its socket closes.
  `session_end` is terminal — that client must not reconnect.
- A connection has 15 s to send a valid `hello`; three rejected hellos end
  the session; the server holds at most 512 connections.

## 2. Stats

| Client → Server | Server → Client |
|---|---|
| `{"t":"stats_get","player_id":123}` (omit for self) | `{"t":"stats_ok","player_id":123,"name":"Alice","wins":10,"losses":4,"draws":1,"elo":1043}` |
| `{"t":"leaderboard_get","limit":50,"online_only":false}` | `{"t":"leaderboard_ok","rows":[{"player_id":..,"name":..,"elo":..,"wins":..,"losses":..,"draws":..,"rank":..,"online":true},...],"self_rank":7,"total":312}` |

Rows are ranked elo desc, wins desc, id asc; `limit` is capped at 200.

## 3. Matchmaking (lobby)

| Client → Server | Server → Client |
|---|---|
| `{"t":"queue_join"}` | `{"t":"queue_ok"}` then, on pairing: `{"t":"match_found","match_id":"m-...","opponent":{"player_id":..,"name":..,"elo":..},"side":0}` |
| `{"t":"queue_leave"}` | `{"t":"queue_ok"}` |

ONE ranked queue. Every 7 s the server sorts everyone waiting by rating
(ties by arrival) and pairs adjacent players — all pairs at once, the odd
player waits for the next pass. `side` is 0 or 1; side 0 is the
higher-rated of the pair and plays first in every round (the campaign's
human seating order, GAME_DESIGN.md §7).

## 4. Setup negotiation

Right after `match_found` the pair agrees on the campaign in two stages —
30 s for the terms, 90 s for the countries — `match_cancelled` reason
`ready_timeout` on expiry; a disconnect cancels with `peer_disconnected`.

1. **Terms** — server → both: `{"t":"setup_role","match_id":...,"chooser":true|false,"deadline_s":30}`
   (one random chooser per pair). The chooser sends
   `{"t":"setup_terms","match_id":...,"data":{"mode":"gdp"|"equality","difficulty":"easy"|"normal"|"hard"}}`;
   bad values answer `setup_err` (`bad_terms` / `not_chooser` /
   `bad_stage`) and may be corrected. Accepted terms echo to BOTH sides as
   `{"t":"setup_terms","match_id":...,"data":{...},"deadline_s":90}`.
2. **Countries** — both send `{"t":"setup_country","match_id":...,"country":"fr"}`.
   An unknown key answers `setup_err` `bad_country`; the same country as the
   opponent's locked pick answers `country_taken`. An accepted pick answers
   `setup_country_ok` (the opponent sees
   `{"t":"setup_opponent_ready","country":"fr"}`); re-sending before the
   opponent commits replaces it.

When both countries are in, the server rolls the seed, records the match
and tells both sides:

- `{"t":"campaign_start","match_id":"m-...","seed":"<uint64 as decimal string>","mode":"gdp","difficulty":"normal","countries":["fr","de"],"names":["Alice","Bob"],"side":0}`

`countries[i]` and `names[i]` are indexed by side. Both clients build the
same `Campaign`: the world of the shared data files (the client's world
hash is part of `hello` as `"world":"<hash>"`; mismatching worlds are
refused at pairing with `match_cancelled` reason `world_mismatch`), the
rolled seed, the agreed terms, humans `[countries[0], countries[1]]`.

## 5. Lockstep

Only one player ever acts at a time (a round is the humans in seating
order, then every AI turn, which both clients compute identically), so the
command log is a simple sequence. A client sends each command the moment
it applied it locally:

- Client → server: `{"t":"cmd","match_id":"m-...","data":<command>}`
- Server → the OTHER client: `{"t":"cmd","match_id":"m-...","seq":41,"side":0,"data":<command>}`

`seq` numbers the log from 1; `side` is the sender's. The server relays
verbatim, in arrival order, and keeps the whole log for the match (§7). A
`cmd` from a session that is not in a live match answers `cmd_err`
`not_in_match`; malformed `data` is relayed anyway — the receiving client
treats a command that the core refuses as a **desync** (§6), never as
something to repair.

### Commands (`data`)

| `kind` | fields | meaning |
|---|---|---|
| `recruit` | `city`, `type` (catalog id), `count` | GAME_DESIGN.md §5.1 |
| `move` | `from`, `to`, `units` (ids) | §5.2 |
| `attack` | `from`, `to`, `units` | §5.3 — may open a battle |
| `end_turn` | | §4 |
| `battle_choice` | `auto`: true/false | the answer to a battle prompt (below) |
| `bcmd` | `cmd`: one board command `{"kind":"rearrange"|"ready"|"promote"|"demote"|"move"|"strike"|"end"|"offer_draw"|"surrender", "side":0/1, "from":{"x","y"}, "to":{"x","y"}, "cell":{"x","y"}, "target":{"x","y"}}` | §8, one applied command, the AI's included |
| `battle_quit` | `side` (board side 0/1) | a side played on that client left the board before Play: it loses the battle as it stands, every unit survives |
| `resign` | | the sender concedes the match; the server rules it at once (§6) |

Player ids never travel: the receiver maps the frame's `side` to its
campaign's human of that seat. The core's wire codec is
`core/include/ad/core/netcodec.hpp`.

### Battles

A defended attack needs a decision — fight on the board or auto-resolve —
and the log carries it as `battle_choice` from the **deciding side**: the
attacker for a human's own attack, the defender for an AI attack on a
human. `auto: true` → both clients auto-resolve locally (deterministic,
node-limited). `auto: false` → the deciding side's client plays the board
and sends every command it applies as `bcmd`, the tactical AI's actions
included, so the other client never runs an AI of its own: it applies the
stream to its copy of the board (the spectator view) and reads the outcome
from it. A battle between the two humans is played by both: each side
sends its own `bcmd`s and applies the other's.

### Round check

At every round end (when the AI turns are done and the round counter
advanced) each client sends `{"t":"sync","match_id":...,"round":12,"hash":"<uint64 hex>"}`.
When both are in, a mismatch is a **desync**: the server sends
`{"t":"desync","match_id":...,"round":12}` to both and finalizes the match
`disputed` (no rating change).

## 6. Results

The clients report and the server records — with two independent cores
replaying one log, a claim is checked by the other side's report:

- Client → server: `{"t":"result","match_id":"m-...","winner_side":0|1|-1,"reason":"domination"|"elimination"|"resign"}`
  from BOTH clients when the campaign ends (GAME_DESIGN.md §9: a
  domination win, or the elimination of one of the two seats).
- Server → both: `{"t":"match_end","match_id":"m-...","winner_side":1,"reason":"...","elo_delta":-12,"disputed":false}`.

Two matching reports finalize; conflicting reports finalize `disputed`
with no rating change, and so does a lone report the other side never
matches within 30 s while it stays connected. A forfeit (§7) and a
`resign` command need no report: the server rules them the moment they
happen. ELO: K = 32, standard expected-score formula, applied once per
match.

## 7. Timeouts & disconnects

- Server pings every 30 s. A connection that dies mid-match starts a
  **60 s** forfeit clock (a strategy game deserves a real grace); the
  survivor sees `{"t":"peer_disconnected","match_id":...,"grace_s":60}`.
  - The dropped player may `hello` again with their token inside the
    window: the server reattaches them and resends `campaign_start` with
    `"replay":[<every cmd frame of the log so far, with seq and side>]`;
    the client rebuilds the campaign from the seed and replays the log
    (its own commands included — they are in the log), then continues.
    The survivor sees `peer_reconnected`.
  - When the clock runs out the server records a loss for the leaver —
    `match_end` reason `forfeit`.
- A disconnect before `campaign_start` cancels the match
  (`match_cancelled` reason `peer_disconnected`).
- The queue entry dies with the connection.

## 8. Rate limit

More than **60 `cmd` frames inside one second** is a flood (a board turn is
a handful of commands). The session gets one final
`{"t":"cmd_err","reason":"rate_limited"}` and is hung up on — mid-match
that walks the §7 path, so a flooder who stays away forfeits. The peer's
address is then refused at accept for 10 s, doubling per repeat up to
1 h; strikes are forgotten an hour after a ban expires. Bans live in server
memory.

## 9. Server storage (SQLite)

```
players(id INTEGER PK, name TEXT UNIQUE, token TEXT, elo INT DEFAULT 1000,
        wins INT, losses INT, draws INT, created_at, last_seen)
matches(id TEXT PK, p0 INT, p1 INT, mode TEXT, difficulty TEXT, seed TEXT,
        c0 TEXT, c1 TEXT, winner INT NULL, reason TEXT, disputed INT DEFAULT 0,
        started_at, ended_at)
```

The command log of a live match lives in server memory (it is gone with a
restart, and so is the match: both clients get `match_cancelled` reason
`server_restart` on their next hello and the row is closed `abandoned`).
