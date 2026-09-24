#!/usr/bin/env python3
"""Fetch the 4x3 SVG flag of every country in assets/world/world.json from
lipis/flag-icons (MIT) into assets/flags/<key>.svg, plus the license text.
Only missing files are downloaded, so re-running is cheap and offline-safe
once the set is complete."""

from __future__ import annotations

import json
import sys
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASE = "https://raw.githubusercontent.com/lipis/flag-icons/main/"
OUT = REPO / "assets" / "flags"


def main() -> int:
    world = json.load(open(REPO / "assets" / "world" / "world.json"))
    OUT.mkdir(parents=True, exist_ok=True)
    lic = OUT / "LICENSE-flag-icons.txt"
    if not lic.exists():
        with urllib.request.urlopen(BASE + "LICENSE", timeout=60) as r:
            lic.write_bytes(r.read())
    missing = []
    for c in world["countries"]:
        dst = OUT / f"{c['key']}.svg"
        if dst.exists():
            continue
        url = f"{BASE}flags/4x3/{c['key']}.svg"
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                dst.write_bytes(r.read())
        except Exception as e:  # noqa: BLE001
            missing.append((c["key"], str(e)))
    for key, err in missing:
        print(f"no flag for {key}: {err}", file=sys.stderr)
    print(f"flags: {len(list(OUT.glob('*.svg')))}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
