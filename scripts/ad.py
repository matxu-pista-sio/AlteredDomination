#!/usr/bin/env python3
"""The developer CLI for Altered Domination.

    ad.py screenshot <page> [--out DIR] [--wait MS] [--then CMD ...]
    ad.py drive CMD [CMD ...]

Runs the built client headless (Xvfb :99, Mesa llvmpipe, AD_DRIVE=1) and
talks to its stdin driver (client/src/devdrive.cpp). See
.claude/skills/share-screenshot/SKILL.md for the command vocabulary.
"""
from __future__ import annotations

import argparse
import os
import select
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "build" / "desktop-debug" / "client" / "altereddomination"
SCRATCH = Path(os.environ.get("AD_SCRATCH", ROOT / "build" / "scratch"))
DISPLAY = os.environ.get("AD_DISPLAY", ":99")


def ensure_xvfb() -> None:
    """Start Xvfb on the display if nothing serves it yet."""
    num = DISPLAY.lstrip(":").split(".")[0]
    if Path(f"/tmp/.X11-unix/X{num}").exists():
        return
    if not shutil.which("Xvfb"):
        sys.exit("Xvfb is not installed (apt install xvfb)")
    subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1400x900x24"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        if Path(f"/tmp/.X11-unix/X{num}").exists():
            return
        time.sleep(0.1)
    sys.exit("Xvfb did not come up")


def run(commands: list[str], startup: float = 3.0) -> int:
    if not BINARY.exists():
        sys.exit(f"{BINARY} is missing: cmake --build --preset desktop-debug first")
    ensure_xvfb()
    SCRATCH.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ,
               DISPLAY=DISPLAY, AD_DRIVE="1", AD_NO_AUDIO="1",
               LIBGL_ALWAYS_SOFTWARE="1", QT_QPA_PLATFORM="xcb", LC_ALL="C.UTF-8",
               AD_SAVE_DIR=str(SCRATCH / "saves"),
               XDG_CONFIG_HOME=str(SCRATCH / "config"))
    qt = os.environ.get("QT_ROOT_DIR")
    if qt:
        env["LD_LIBRARY_PATH"] = f"{qt}/lib:" + env.get("LD_LIBRARY_PATH", "")
    proc = subprocess.Popen([str(BINARY)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=env, text=True, bufsize=1)
    errors: list[str] = []
    threading.Thread(target=lambda: errors.extend(l.rstrip() for l in proc.stderr), daemon=True).start()
    time.sleep(startup)
    failed = 0

    def send(cmd: str, timeout: float = 60.0) -> None:
        nonlocal failed
        try:
            proc.stdin.write(cmd + "\n")
            proc.stdin.flush()
        except BrokenPipeError:
            print(f"{cmd!r}: the client is gone")
            failed += 1
            return
        ready, _, _ = select.select([proc.stdout], [], [], timeout)
        if not ready:
            print(f"{cmd!r}: timeout")
            failed += 1
            return
        line = proc.stdout.readline().rstrip()
        print(f"{cmd!r}: {line}")
        if line.startswith("err"):
            failed += 1

    send("activate")
    for cmd in commands:
        send(cmd)
    send("quit")
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    time.sleep(0.2)
    if errors:
        print("---- client stderr ----")
        print("\n".join(errors[-80:]))
    return 1 if failed else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("screenshot", help="screenshot a page")
    s.add_argument("page")
    s.add_argument("--out", default=str(ROOT / "shots"))
    s.add_argument("--wait", type=int, default=1500, help="ms to let the page settle")
    s.add_argument("--size", default="1280x800")
    s.add_argument("--then", nargs="*", default=[], help="driver commands to run before the shot")
    d = sub.add_parser("drive", help="run driver commands")
    d.add_argument("commands", nargs="+")
    args = ap.parse_args()

    if args.cmd == "screenshot":
        out = Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        w, h = args.size.split("x")
        cmds = [f"size {w} {h}", f"page {args.page}", f"wait {args.wait}"]
        cmds += list(args.then)
        cmds.append(f"shot {out / (args.page + '.png')}")
        return run(cmds)
    return run(args.commands)


if __name__ == "__main__":
    sys.exit(main())
