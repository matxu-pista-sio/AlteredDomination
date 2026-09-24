#!/usr/bin/env python3
"""Validate assets/units/units.json (docs/DATA_PIPELINE.md §7): every unit
has a key, name, cost, description, exactly one of human/machine and one of
land/air, integer move/strike offsets with path offsets, strike affects
within the class set, no duplicate offsets, and an icon file. Exit 1 on any
failure."""

from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UNITS = REPO / "assets" / "units"


def main() -> int:
    doc = json.load(open(UNITS / "units.json"))
    classes = set(doc["classes"])
    errors: list[str] = []
    keys = set()
    for u in doc["units"]:
        k = u.get("key", "?")
        if k in keys:
            errors.append(f"duplicate key {k}")
        keys.add(k)
        for field in ("name", "cost", "description", "classes", "moves", "strikes"):
            if field not in u:
                errors.append(f"{k}: missing {field}")
        if not isinstance(u.get("cost"), int) or u["cost"] <= 0:
            errors.append(f"{k}: cost must be a positive integer")
        cs = set(u.get("classes", []))
        if not cs <= classes:
            errors.append(f"{k}: unknown class {cs - classes}")
        if len(cs & {"human", "machine"}) != 1 or len(cs & {"land", "air"}) != 1:
            errors.append(f"{k}: needs exactly one of human/machine and one of land/air")
        for kind in ("moves", "strikes"):
            seen = set()
            for e in u.get(kind, []):
                dx, dy = e.get("dx"), e.get("dy")
                if not isinstance(dx, int) or not isinstance(dy, int) or (dx, dy) == (0, 0):
                    errors.append(f"{k}: bad {kind} offset {dx},{dy}")
                if (dx, dy) in seen:
                    errors.append(f"{k}: duplicate {kind} offset {dx},{dy}")
                seen.add((dx, dy))
                for p in e.get("path", []):
                    if (not isinstance(p, list) or len(p) != 2
                            or not all(isinstance(v, int) for v in p)):
                        errors.append(f"{k}: bad path entry in {kind} {dx},{dy}")
                if kind == "strikes":
                    aff = set(e.get("affects", []))
                    if not aff or not aff <= classes:
                        errors.append(f"{k}: strike {dx},{dy} affects {aff}")
        if not (UNITS / "icons" / f"{k}.svg").exists():
            errors.append(f"{k}: missing icon assets/units/icons/{k}.svg")
    if len(keys) != 11:
        errors.append(f"expected 11 unit types, found {len(keys)}")
    for e in errors[:50]:
        print("ERROR:", e, file=sys.stderr)
    print(f"units: {len(keys)} types - {'OK' if not errors else str(len(errors)) + ' errors'}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
