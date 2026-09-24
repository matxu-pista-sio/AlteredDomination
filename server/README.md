# ad-server

The headless online server for Altered Domination (docs/PROTOCOL.md):
accounts and ELO, one ranked queue, the setup negotiation, and the relay
of each match's command log. It never simulates a campaign - both clients
run the same deterministic core on the same seed and the server checks
that their round hashes agree.

## Build

The server needs Qt 6.8+ with the `Core`, `WebSockets` and `Sql` modules
(the SQLite driver ships with `Sql`); no GUI modules.

```
export QT_ROOT_DIR=/path/to/Qt/6.8/gcc_64
cmake --preset server-release && cmake --build --preset server-release
./build/server-release/server/ad-server --port 9977 --db /var/lib/ad/ad-server.sqlite
```

`desktop-debug` builds it alongside the client, with the test suite:

```
cmake --preset desktop-debug && cmake --build --preset desktop-debug
./build/desktop-debug/server/tests/ad-server-tests
```

## Options

| flag | default | |
|---|---|---|
| `--port`, `-p` | `9977` | WebSocket listen port |
| `--db`, `-d` | `./ad-server.sqlite` | SQLite file, created on first run |
| `--world`, `-w` | the repository's `assets/world/world.json` | validates country picks |
| `--verbose` | off | frame-level logging (tokens are redacted) |

## Running it for real

- Put it behind a TLS-terminating reverse proxy (nginx, caddy) so clients
  connect with `wss://`; the server itself speaks plain `ws://` on the
  loopback interface. See docs/issues/28-owner-host-the-server.md.
- One process, one database. The command log of a live match is in
  memory: a restart abandons every open match (both players are told
  `server_restart` on their next hello).
- Logs are one line per event (`event=... key=value`) on stderr.

## Tests

`server/tests/` is a doctest suite over a live `QCoreApplication`: real
`QWebSocket` clients speak real frames to a real listening server on an
ephemeral port, with the clocks shrunk to milliseconds
(`adtest::fastTuning()`). Run it with `AD_SERVER_TEST_VERBOSE=1` to see
the server's log lines.
