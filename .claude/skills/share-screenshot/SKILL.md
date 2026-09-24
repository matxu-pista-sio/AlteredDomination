---
name: share-screenshot
description: Screenshot the running Altered Domination client headless (Xvfb + Mesa) to verify a client change, and attach the PNG to the ticket or the reply.
---

# Share a screenshot of the client

Every client change is verified by running the real app and looking at it.
The client has a built-in driver (`client/src/devdrive.cpp`), armed by
`AD_DRIVE=1`, that reads commands on stdin and answers on stdout; it works
with no window manager and no GPU.

## One-shot: `scripts/ad.py screenshot`

```
python3 scripts/ad.py screenshot home                # -> shots/home.png
python3 scripts/ad.py screenshot campaign-fr --wait 2500
python3 scripts/ad.py screenshot battle --then "key Return" "wait 1500"
python3 scripts/ad.py drive "page newgame" "type ja" "wait 400" "shot search.png"
```

Pages: `intro`, `home`, `newgame`, `load`, `settings`, `codex`, `gallery`,
`campaign` (random country, seed 7), `campaign-fr` (France, seed 7),
`battle` (France's capital assaults its first defended neighbour, board up).

The tool starts `Xvfb :99` if nothing listens there, builds nothing (build
first with `cmake --build --preset desktop-debug`), sets `AD_NO_AUDIO=1`
and an isolated `AD_SAVE_DIR`/`XDG_CONFIG_HOME` under the scratch
directory, and prints the client's stderr, so QML warnings are part of
the result.

## Driver commands

```
shot <path>              render the window to a PNG
click <x> <y>            left click at window coordinates (rclick: right)
move <x> <y>             hover
wheel <x> <y> <delta>    wheel (120 = one notch)
key <name>               QKeySequence name: E, Return, Escape, Ctrl+S, ...
type <text>              text into the focused field
size <w> <h>             window size
activate                 give the window keyboard focus (Xvfb has no WM)
page <name>              jump to a page (Main.qml devPage)
eval <js>                evaluate in the window scope; `game` is GameController, `net` is LobbyClient
wait <ms>                answer after a delay (animations settle)
quit
```

`eval` is the way to read state: `eval game.round`, `eval game.battle.phase`,
`eval JSON.stringify(game.attackTargets(351))`.

## Reading the result

- Compare against the design docs (`docs/UI_THEME.md`): no hex colours in
  QML, the map owns green/red/cyan, lit means brass.
- A QML warning on stderr is a bug in the change, not noise.
- Attach the PNG to the ticket's done-record or the reply; name it after
  the page and the state (`campaign-recruit.png`).
