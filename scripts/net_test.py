#!/usr/bin/env python3
"""Two headless clients through the online flow, against a local ad-server.

    net_test.py [--out DIR] [--keep]

Starts ad-server on a free port, launches two clients (each with its own
settings, so each registers its own account) pointed at it through
DEV_SERVER_ADDRESS, and drives them with the AD_DRIVE stdin driver
(client/src/devdrive.cpp): lobby -> ranked queue -> the setup negotiation
-> the shared campaign -> a turn each -> the AI round on both machines ->
the server's round-hash check -> a resignation and the verdict. Every step
is asserted through `eval`; screenshots of both windows land in --out.

Exit status 0 = the whole flow held, and the two campaigns agreed on the
round hash (docs/PROTOCOL.md §5 "Round check").
"""
from __future__ import annotations

import argparse
import os
import select
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ad  # noqa: E402  (scripts/ad.py: ROOT, BINARY, ensure_xvfb, DISPLAY)

SERVER = ad.ROOT / "build" / "desktop-debug" / "server" / "ad-server"
SCRATCH = Path(os.environ.get("AD_SCRATCH", ad.ROOT / "build" / "scratch")) / "net"


class Failure(Exception):
    pass


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Client:
    """One headless client with its own settings and save directories."""

    def __init__(self, tag: str, port: int, startup: float = 3.0):
        home = SCRATCH / tag
        (home / "config").mkdir(parents=True, exist_ok=True)
        env = dict(os.environ,
                   DISPLAY=ad.DISPLAY, AD_DRIVE="1", AD_NO_AUDIO="1",
                   LIBGL_ALWAYS_SOFTWARE="1", QT_QPA_PLATFORM="xcb", LC_ALL="C.UTF-8",
                   AD_SAVE_DIR=str(home / "saves"),
                   XDG_CONFIG_HOME=str(home / "config"),
                   DEV_SERVER_ADDRESS=f"127.0.0.1:{port}")
        qt = os.environ.get("QT_ROOT_DIR")
        if qt:
            env["LD_LIBRARY_PATH"] = f"{qt}/lib:" + env.get("LD_LIBRARY_PATH", "")
        self.tag = tag
        self.log = open(home / "client.log", "w")
        self.proc = subprocess.Popen([str(ad.BINARY)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.log, env=env, text=True, bufsize=1)
        time.sleep(startup)

    def send(self, cmd: str, timeout: float = 60.0) -> str:
        assert self.proc.stdin and self.proc.stdout
        try:
            self.proc.stdin.write(cmd + "\n")
            self.proc.stdin.flush()
        except BrokenPipeError as e:
            raise Failure(f"{self.tag}: the client is gone ({cmd!r})") from e
        ready, _, _ = select.select([self.proc.stdout], [], [], timeout)
        if not ready:
            raise Failure(f"{self.tag}: timeout on {cmd!r}")
        line = self.proc.stdout.readline().rstrip()
        if line.startswith("err"):
            raise Failure(f"{self.tag}: {cmd!r} -> {line}")
        return line

    def eval(self, js: str) -> str:
        # the driver answers "ok eval <value>"; an empty value leaves "ok eval"
        line = self.send("eval " + js)
        return line[len("ok eval"):].lstrip(" ") if line.startswith("ok eval") else line

    def wait_until(self, js: str, expected: str, timeout: float = 30.0, every: float = 0.4) -> None:
        deadline = time.monotonic() + timeout
        last = ""
        while time.monotonic() < deadline:
            last = self.eval(js)
            if last == expected:
                return
            self.send(f"wait {int(every * 1000)}")
        raise Failure(f"{self.tag}: {js} stayed {last!r}, wanted {expected!r}")

    def shot(self, path: Path) -> None:
        self.send(f"shot {path}")

    def close(self) -> None:
        try:
            self.send("quit", timeout=5)
        except Failure:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ad.ROOT / "shots" / "net"))
    ap.add_argument("--keep", action="store_true", help="keep the server's database")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for binary in (ad.BINARY, SERVER):
        if not binary.exists():
            sys.exit(f"{binary} is missing: cmake --build --preset desktop-debug first")
    ad.ensure_xvfb()
    SCRATCH.mkdir(parents=True, exist_ok=True)

    port = free_port()
    db = SCRATCH / "ad-server.sqlite"
    if not args.keep:
        for p in SCRATCH.glob("ad-server.sqlite*"):
            p.unlink()
    env = dict(os.environ)
    qt = os.environ.get("QT_ROOT_DIR")
    if qt:
        env["LD_LIBRARY_PATH"] = f"{qt}/lib:" + env.get("LD_LIBRARY_PATH", "")
    server = subprocess.Popen([str(SERVER), "--port", str(port), "--db", str(db)],
                              stdout=subprocess.DEVNULL, stderr=open(SCRATCH / "server.log", "w"), env=env)
    time.sleep(0.8)
    if server.poll() is not None:
        sys.exit(f"ad-server exited early; see {SCRATCH / 'server.log'}")
    print(f"ad-server on 127.0.0.1:{port}")

    clients: list[Client] = []
    try:
        a = Client("alice", port)
        b = Client("bob", port)
        clients += [a, b]
        step = 0

        def check(label: str) -> None:
            nonlocal step
            step += 1
            print(f"  [{step:2}] {label}")

        for c, name in ((a, "Alice"), (b, "Bob")):
            c.send("activate")
            c.send("size 1280 800")
            c.eval(f'net.playerName = "{name}"')
            c.send("page lobby")
        for c in (a, b):
            c.wait_until("net.connectionState", "3", timeout=15)   # Online
        check("both clients online")
        a.wait_until("net.selfStats.elo", "1000", timeout=10)
        a.shot(out / "lobby-online.png")

        a.eval("net.joinQueue()")
        b.eval("net.joinQueue()")
        for c in (a, b):
            c.wait_until("net.setupStage", "1", timeout=20)        # Terms (a pass every 7 s)
        check("paired; the setup opened")
        a.shot(out / "lobby-paired.png")

        chooser = a if a.eval("net.setupChooser") == "true" else b
        other = b if chooser is a else a
        if other.eval("net.setupChooser") != "false":
            raise Failure("both clients think they choose the terms")
        chooser.eval('net.setTerms("gdp", "normal")')
        for c in (a, b):
            c.wait_until("net.setupStage", "2", timeout=10)        # Countries
        check("terms agreed by both")
        b.shot(out / "lobby-countries.png")

        a.eval('net.pickCountry("fr")')
        a.wait_until("net.myCountry", "fr", timeout=10)
        b.wait_until("net.opponentCountry", "fr", timeout=10)
        b.eval('net.pickCountry("fr")')                            # taken: refused
        b.send("wait 500")
        if b.eval("net.myCountry") != "":
            raise Failure("the server accepted a taken banner")
        b.eval('net.pickCountry("de")')
        for c in (a, b):
            c.wait_until("game.online", "true", timeout=15)
        check("campaign_start on both; the same seed")
        if a.eval("game.seedText") != b.eval("game.seedText"):
            raise Failure("the two clients got different seeds")
        a.send("wait 1500")
        b.send("wait 1500")
        a.shot(out / "campaign-alice.png")
        b.shot(out / "campaign-bob.png")

        first = a if a.eval("game.onlineSide") == "0" else b
        second = b if first is a else a
        first.wait_until("game.humanTurn", "true", timeout=10)
        if second.eval("game.remoteTurn") != "true":
            raise Failure("the second seat does not see the first one's turn")
        check(f"{first.tag} opens the round; {second.tag} waits")

        capital = first.eval("game.capitalOf(game.humanKey)")
        if first.eval(f'game.recruit({capital}, "soldier", 1)') != "":
            raise Failure("the first seat could not recruit")
        first.eval("game.endTurn()")
        second.wait_until("game.humanTurn", "true", timeout=15)
        # the recruit was relayed and applied: the other side sees the unit
        if second.eval(f"game.cityInfo({capital}).unitCount") != first.eval(f"game.cityInfo({capital}).unitCount"):
            raise Failure("the recruit did not reach the other client")
        check("a recruit and an end turn relayed; the second seat's turn")
        second.shot(out / "campaign-my-turn.png")

        second.eval("game.endTurn()")
        # The AI round runs on both machines. An AI power may attack one of
        # the seats: the deciding client answers its prompt (auto-resolve,
        # which both clients then compute), like a player would.
        deadline = time.monotonic() + 600
        prompts = 0
        while True:
            rounds = {c.tag: c.eval("game.round") for c in (a, b)}
            if all(r == "2" for r in rounds.values()) and all(c.eval("game.aiThinking") == "false" for c in (a, b)):
                break
            for c in (a, b):
                if c.eval("game.onlineOver") == "true":
                    raise Failure(f"{c.tag}: the match ended during the AI round: {c.eval('game.onlineOutcome.reason')}")
                if c.eval("game.battleOffered") == "true" and c.eval("game.battleDecisionMine") == "true":
                    c.eval("game.acceptBattle(true)")
                    prompts += 1
            if time.monotonic() > deadline:
                raise Failure(f"the AI round never finished (rounds {rounds})")
            a.send("wait 1000")
        check(f"the AI round ran on both machines ({prompts} battle prompt{'s' if prompts != 1 else ''} answered)")
        a.send("wait 1500")
        b.send("wait 1500")
        for c in (a, b):
            if c.eval("game.onlineOver") == "true":
                raise Failure(f"{c.tag}: the match ended early: {c.eval('game.onlineOutcome.reason')}")
        check("round 2 on both after the AI round; no desync reported")
        first.shot(out / "campaign-round2.png")

        second.eval("game.resign()")
        for c in (a, b):
            c.wait_until("game.onlineOver", "true", timeout=15)
        if first.eval("game.onlineOutcome.won") != "true" or second.eval("game.onlineOutcome.won") != "false":
            raise Failure("the verdict went the wrong way")
        if first.eval("game.onlineOutcome.reason") != "resign":
            raise Failure("the verdict's reason is not the resignation")
        check("resignation ruled: a win and a loss")
        first.send("wait 800")
        first.shot(out / "verdict-won.png")
        second.shot(out / "verdict-lost.png")

        # back in the lobby, the ladder moved
        second.eval("game.leaveGame()")
        second.send("page lobby")
        second.wait_until("net.selfStats.losses", "1", timeout=15)
        first.eval("game.leaveGame()")
        first.send("page lobby")
        first.wait_until("net.selfStats.wins", "1", timeout=15)
        first.send("wait 600")
        first.shot(out / "lobby-after.png")
        check("the records moved: 1 win, 1 loss")
        print("PASS")
        return 0
    except Failure as e:
        print(f"FAIL: {e}")
        for c in clients:
            try:
                c.shot(out / f"failure-{c.tag}.png")
            except Failure:
                pass
            c.log.flush()
            lines = (SCRATCH / c.tag / "client.log").read_text().splitlines()
            if lines:
                print(f"---- {c.tag} stderr ----")
                print("\n".join(lines[-15:]))
        print(f"server log: {SCRATCH / 'server.log'}")
        return 1
    finally:
        for c in clients:
            c.close()
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    sys.exit(main())
