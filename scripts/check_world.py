#!/usr/bin/env python3
"""Validate the committed world data (docs/DATA_PIPELINE.md): unique keys,
dense ids, every country has a capital and at least one city, links are
stored once with a < b, the link graph is connected and no city is isolated,
territories/outlines exist for every city/country, adjacent countries do not
share a banner hue, the content hash matches. Exit 1 on any failure."""

from __future__ import annotations

import hashlib
import json
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORLD = REPO / "assets" / "world"


def hue(hex_colour: str) -> float:
    r, g, b = (int(hex_colour[i:i + 2], 16) / 255 for i in (1, 3, 5))
    mx, mn = max(r, g, b), min(r, g, b)
    if mx == mn:
        return 0.0
    d = mx - mn
    if mx == r:
        h = (g - b) / d % 6
    elif mx == g:
        h = (b - r) / d + 2
    else:
        h = (r - g) / d + 4
    return h * 60


def main() -> int:
    errors: list[str] = []
    world = json.load(open(WORLD / "world.json"))
    terr = json.load(open(WORLD / "territories.json"))
    outl = json.load(open(WORLD / "outlines.json"))
    countries, cities, links = world["countries"], world["cities"], world["links"]

    canon = json.dumps({k: world[k] for k in ("countries", "cities", "links")},
                       sort_keys=True, separators=(",", ":"))
    if hashlib.sha256(canon.encode()).hexdigest() != world["hash"]:
        errors.append("hash does not match the content")

    ckeys = [c["key"] for c in countries]
    if len(set(ckeys)) != len(ckeys):
        errors.append("duplicate country keys")
    if ckeys != sorted(ckeys):
        errors.append("countries are not sorted by key")
    if [c["id"] for c in cities] != list(range(len(cities))):
        errors.append("city ids are not dense 0..N-1")
    keys = [c["key"] for c in cities]
    if len(set(keys)) != len(keys):
        errors.append("duplicate city keys")
    by_country = defaultdict(list)
    for c in cities:
        by_country[c["country"]].append(c)
        if c["country"] not in set(ckeys):
            errors.append(f"city {c['key']} belongs to unknown country {c['country']}")
        if c["key"] not in terr or not terr[c["key"]]:
            errors.append(f"no territory for {c['key']}")
    for c in countries:
        mine = by_country[c["key"]]
        if not mine:
            errors.append(f"country {c['key']} has no city")
        cap = cities[c["capital"]] if 0 <= c["capital"] < len(cities) else None
        if cap is None or cap["country"] != c["key"] or not cap["capital"]:
            errors.append(f"country {c['key']} has no valid capital")
        if c["key"] not in outl or not outl[c["key"]]:
            errors.append(f"no outline for {c['key']}")
        if c["gdp"] <= 0 or c["population"] <= 0:
            errors.append(f"country {c['key']} has no gdp/population")

    seen = set()
    adj = defaultdict(set)
    for l in links:
        a, b = l["a"], l["b"]
        if not (0 <= a < b < len(cities)):
            errors.append(f"bad link {a}-{b}")
            continue
        if (a, b) in seen:
            errors.append(f"duplicate link {a}-{b}")
        seen.add((a, b))
        adj[a].add(b)
        adj[b].add(a)
    for c in cities:
        if not adj[c["id"]]:
            errors.append(f"isolated city {c['key']}")
    stack, visited = [0], {0}
    while stack:
        n = stack.pop()
        for m in adj[n]:
            if m not in visited:
                visited.add(m)
                stack.append(m)
    if len(visited) != len(cities):
        errors.append(f"link graph not connected: {len(visited)}/{len(cities)} reachable")

    # banner hues of countries that share a link must differ
    colour = {c["key"]: c["color"] for c in countries}
    for l in links:
        a, b = cities[l["a"]]["country"], cities[l["b"]]["country"]
        if a != b and abs(hue(colour[a]) - hue(colour[b])) < 1e-6:
            errors.append(f"linked countries {a} and {b} share a hue")

    n_countries, n_cities = len(countries), len(cities)
    if not (170 <= n_countries <= 215):
        errors.append(f"unexpected country count {n_countries}")
    if not (900 <= n_cities <= 1100):
        errors.append(f"unexpected city count {n_cities}")

    for e in errors[:50]:
        print("ERROR:", e, file=sys.stderr)
    print(f"world: {n_countries} countries, {n_cities} cities, {len(links)} links"
          f" - {'OK' if not errors else str(len(errors)) + ' errors'}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
