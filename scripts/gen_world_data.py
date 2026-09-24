#!/usr/bin/env python3
"""Generate the game world from public data (docs/DATA_PIPELINE.md).

Inputs (cached in .cache/raw/, fetched with --fetch from the pinned URLs):
  * Natural Earth 1:50m admin_0_countries  - polygons, ISO codes, POP_EST, GDP_MD
  * Natural Earth 1:10m populated_places_simple - cities, lon/lat, pop_max, capitals
  * datasets/gdp gdp.csv - World Bank NY.GDP.MKTP.CD, latest year per ISO3

Outputs (committed):
  * assets/world/world.json        countries, cities, links, projection, hash
  * assets/world/territories.json  one SVG path per city (Voronoi cell clipped
                                   to its country)
  * assets/world/outlines.json     one SVG path per country

Deterministic: pure Python + Shapely, no randomness, sorted everywhere.
Usage: python3 scripts/gen_world_data.py [--fetch] [--stats]
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import sys
import unicodedata
import urllib.request
from collections import defaultdict
from pathlib import Path

import numpy as np
import shapely
from shapely.geometry import (GeometryCollection, LineString, MultiPoint,
                              MultiPolygon, Point, Polygon, box, shape)
from shapely.ops import unary_union, voronoi_diagram
from shapely.prepared import prep

REPO = Path(__file__).resolve().parent.parent
RAW = REPO / ".cache" / "raw"
OUT = REPO / "assets" / "world"

# --- sources (pinned) -------------------------------------------------------
NE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/"
SOURCES = {
    "ne_50m_admin_0_countries.geojson": NE + "ne_50m_admin_0_countries.geojson",
    "ne_10m_populated_places_simple.geojson":
        NE + "ne_10m_populated_places_simple.geojson",
    "gdp.csv": "https://raw.githubusercontent.com/datasets/gdp/main/data/gdp.csv",
}

# --- the numbers of docs/DATA_PIPELINE.md -----------------------------------
MAP_WIDTH = 4096.0
LAT_MIN, LAT_MAX = -56.0, 84.0
MIN_COUNTRY_POP = 50_000
CITY_BUDGET_TARGET = 1000
CITY_QUOTA_MAX = 60
MIN_CITY_SPACING = 14.0      # map units
LAND_LINK = 140.0            # map units: Gabriel edges up to this are kept
WRAP_LINK = 220.0            # antimeridian pairs up to this wrapped distance
SIMPLIFY_TOL = 0.8           # map units, applied to country polygons
TIER_BOUNDS = (300_000, 1_000_000, 5_000_000)
PLACE_CLASSES = {
    "Populated place", "Populated Place", "Admin-0 capital", "Admin-1 capital",
    "Admin-0 region capital", "Admin-1 region capital", "Admin-0 capital alt",
}
# Natural Earth features folded into the country that administers them on
# the game map (no ISO code of their own).
FOLD_ADM0 = {"SOL": "SOM", "CYN": "CYP", "KAS": "IND"}
# Natural Earth ADM0_A3 -> World Bank ISO3 where they differ.
WB_ISO3 = {"KOS": "XKX", "SDS": "SSD", "PSX": "PSE", "SAH": "ESH", "CYN": "CYP",
           "SOL": "SOM"}
# 48 banner hues: 24 hues at two lightness levels, saturated but not neon.
PALETTE = []
for _i in range(24):
    _h = (_i * 360 / 24 + 7) % 360
    PALETTE.append(_h)


# --- helpers ----------------------------------------------------------------
def fetch_all() -> None:
    RAW.mkdir(parents=True, exist_ok=True)
    for name, url in SOURCES.items():
        dst = RAW / name
        if dst.exists():
            continue
        print(f"fetching {name} ...", flush=True)
        with urllib.request.urlopen(url, timeout=300) as r, open(dst, "wb") as f:
            f.write(r.read())


def y_miller(lat_deg: float) -> float:
    phi = math.radians(lat_deg)
    return 1.25 * math.log(math.tan(math.pi / 4 + 0.4 * phi))


SCALE = MAP_WIDTH / (2 * math.pi)
Y_TOP = y_miller(LAT_MAX)
MAP_HEIGHT = (Y_TOP - y_miller(LAT_MIN)) * SCALE


def project(lon: float, lat: float) -> tuple[float, float]:
    lat = max(LAT_MIN, min(LAT_MAX, lat))
    x = (lon + 180.0) / 360.0 * MAP_WIDTH
    y = (Y_TOP - y_miller(lat)) * SCALE
    return x, y


def project_geom(geom):
    def f(pts):
        lon = pts[:, 0]
        lat = np.clip(pts[:, 1], LAT_MIN, LAT_MAX)
        x = (lon + 180.0) / 360.0 * MAP_WIDTH
        ym = 1.25 * np.log(np.tan(np.pi / 4 + 0.4 * np.radians(lat)))
        y = (Y_TOP - ym) * SCALE
        return np.column_stack([x, y])
    return shapely.transform(geom, f)


def equal_area(geom):
    """Cylindrical equal-area transform: areas are proportional to real ones."""
    def f(pts):
        return np.column_stack([pts[:, 0], np.sin(np.radians(pts[:, 1])) * 57.29578])
    return shapely.transform(geom, f)


def slug(s: str) -> str:
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    out = []
    for ch in s.lower():
        out.append(ch if ch.isalnum() else "-")
    r = "".join(out).strip("-")
    while "--" in r:
        r = r.replace("--", "-")
    return r or "city"


def fmt(v: float) -> str:
    s = f"{v:.1f}"
    return s[:-2] if s.endswith(".0") else s


def ring_to_path(coords) -> str:
    pts = list(coords)
    if len(pts) > 1 and pts[0] == pts[-1]:
        pts = pts[:-1]
    if len(pts) < 3:
        return ""
    return "M" + " L".join(f"{fmt(x)} {fmt(y)}" for x, y in pts) + " Z"


def geom_to_path(geom) -> str:
    parts = []
    polys = list(geom.geoms) if isinstance(geom, MultiPolygon) else [geom]
    for poly in polys:
        if poly.is_empty or not isinstance(poly, Polygon):
            continue
        p = ring_to_path(poly.exterior.coords)
        if p:
            parts.append(p)
        for hole in poly.interiors:
            h = ring_to_path(hole.coords)
            if h:
                parts.append(h)
    return " ".join(parts)


def polys_only(geom):
    """Drop points/lines that intersections can produce; keep polygons."""
    if geom.is_empty:
        return geom
    if isinstance(geom, (Polygon, MultiPolygon)):
        return geom
    if isinstance(geom, GeometryCollection):
        ps = [g for g in geom.geoms if isinstance(g, (Polygon, MultiPolygon))]
        return unary_union(ps) if ps else Polygon()
    return Polygon()


def hsl_to_hex(h: float, s: float, l: float) -> str:
    c = (1 - abs(2 * l - 1)) * s
    hp = (h % 360) / 60
    x = c * (1 - abs(hp % 2 - 1))
    r, g, b = {0: (c, x, 0), 1: (x, c, 0), 2: (0, c, x), 3: (0, x, c),
               4: (x, 0, c), 5: (c, 0, x)}[int(hp) % 6]
    m = l - c / 2
    return "#%02x%02x%02x" % tuple(int(round((v + m) * 255)) for v in (r, g, b))


def tier_of(pop: int) -> int:
    t = 0
    for bound in TIER_BOUNDS:
        if pop >= bound:
            t += 1
    return t


# --- countries --------------------------------------------------------------
def load_countries():
    data = json.load(open(RAW / "ne_50m_admin_0_countries.geojson"))
    feats = []
    for f in data["features"]:
        p = f["properties"]
        adm = p["ADM0_A3"]
        iso = p.get("ISO_A2_EH") or p.get("ISO_A2") or "-99"
        feats.append({
            "adm0": adm, "iso2": iso.lower() if iso != "-99" else None,
            "name": p.get("NAME_LONG") or p["NAME"], "short": p["NAME"],
            "pop": float(p.get("POP_EST") or 0), "gdp_md": float(p.get("GDP_MD") or 0),
            "continent": p.get("CONTINENT", ""), "type": p.get("TYPE", ""),
            "sovereign": p.get("SOV_A3", adm),
            "geom": shape(f["geometry"]),
        })
    by_adm = {c["adm0"]: c for c in feats}
    # fold the code-less territories into their administrators
    for src, dst in FOLD_ADM0.items():
        if src in by_adm and dst in by_adm:
            by_adm[dst]["geom"] = unary_union([by_adm[dst]["geom"], by_adm[src]["geom"]])
    selected = {}
    for c in sorted(feats, key=lambda c: -c["pop"]):
        if c["adm0"] in FOLD_ADM0 or c["iso2"] is None or c["iso2"] == "aq":
            continue
        if c["pop"] < MIN_COUNTRY_POP:
            continue
        if c["iso2"] in selected:  # a smaller feature reusing a code (Ashmore -> au)
            continue
        selected[c["iso2"]] = c
    return selected, by_adm


def load_gdp():
    latest = {}
    with open(RAW / "gdp.csv", newline="") as f:
        for row in csv.DictReader(f):
            code, year, value = row["Country Code"], int(row["Year"]), row["Value"]
            if not value:
                continue
            if code not in latest or year > latest[code][0]:
                latest[code] = (year, float(value))
    return latest


# --- cities -----------------------------------------------------------------
def load_places(countries, by_adm):
    data = json.load(open(RAW / "ne_10m_populated_places_simple.geojson"))
    adm_to_iso = {c["adm0"]: iso for iso, c in countries.items()}
    for src, dst in FOLD_ADM0.items():
        if dst in adm_to_iso:
            adm_to_iso[src] = adm_to_iso[dst]
    prepared = {iso: prep(c["geom"]) for iso, c in countries.items()}
    places = defaultdict(list)
    for f in data["features"]:
        p = f["properties"]
        if p.get("featurecla") not in PLACE_CLASSES:
            continue
        lon, lat = f["geometry"]["coordinates"]
        if lat < LAT_MIN or lat > LAT_MAX:
            continue
        iso = adm_to_iso.get(p.get("adm0_a3"))
        if iso is None:
            iso2 = (p.get("iso_a2") or "").lower()
            iso = iso2 if iso2 in countries else None
        if iso is None:
            pt = Point(lon, lat)
            for cand, pg in prepared.items():
                if pg.contains(pt):
                    iso = cand
                    break
        if iso is None:
            continue
        places[iso].append({
            "name": " ".join(p["name"].split()), "lon": lon, "lat": lat,
            "pop": int(p.get("pop_max") or 0),
            "capital": int(p.get("adm0cap") or 0) == 1,
            "rank": int(p.get("scalerank") or 10),
        })
    return places


def choose_cities(countries, places):
    world_pop = sum(c["pop"] for c in countries.values())
    areas = {iso: equal_area(c["geom"]).area for iso, c in countries.items()}
    world_area = sum(areas.values())

    def quotas(budget: float):
        q = {}
        for iso, c in countries.items():
            share = 0.55 * c["pop"] / world_pop + 0.45 * areas[iso] / world_area
            q[iso] = max(1, min(CITY_QUOTA_MAX, round(budget * share)))
        return q

    lo, hi = 200.0, 5000.0
    for _ in range(60):
        mid = (lo + hi) / 2
        total = sum(quotas(mid).values())
        if total < CITY_BUDGET_TARGET:
            lo = mid
        else:
            hi = mid
    quota = quotas(hi)

    accepted = []          # dicts with x, y
    accepted_pts = []

    def too_close(x, y):
        for ax, ay in accepted_pts:
            if (ax - x) ** 2 + (ay - y) ** 2 < MIN_CITY_SPACING ** 2:
                return True
        return False

    # capitals first, always accepted
    for iso in sorted(countries):
        cands = sorted(places.get(iso, []),
                       key=lambda p: (not p["capital"], -p["pop"], p["name"]))
        if not cands:
            continue
        cap = cands[0]
        x, y = project(cap["lon"], cap["lat"])
        accepted.append({**cap, "iso": iso, "x": x, "y": y, "capital": True})
        accepted_pts.append((x, y))
    taken = defaultdict(int)
    for c in accepted:
        taken[c["iso"]] += 1
    # then everything else, biggest first across the world
    rest = []
    for iso, lst in places.items():
        cands = sorted(lst, key=lambda p: (not p["capital"], -p["pop"], p["name"]))
        for p in cands[1:]:
            rest.append((iso, p))
    rest.sort(key=lambda t: (-t[1]["pop"], t[0], t[1]["name"]))
    for iso, p in rest:
        if taken[iso] >= quota[iso]:
            continue
        x, y = project(p["lon"], p["lat"])
        if too_close(x, y):
            continue
        accepted.append({**p, "iso": iso, "x": x, "y": y, "capital": False})
        accepted_pts.append((x, y))
        taken[iso] += 1
    # stable order: by country key, then capital, then pop desc, then name
    accepted.sort(key=lambda c: (c["iso"], not c["capital"], -c["pop"], c["name"]))
    return accepted, quota


# --- links ------------------------------------------------------------------
class DSU:
    def __init__(self, n):
        self.p = list(range(n))

    def find(self, a):
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def union(self, a, b):
        a, b = self.find(a), self.find(b)
        if a == b:
            return False
        self.p[b] = a
        return True


def build_links(cities, land_union):
    pts = [(c["x"], c["y"]) for c in cities]
    index = {pt: i for i, pt in enumerate(pts)}
    tri = shapely.delaunay_triangles(MultiPoint(pts), only_edges=True)
    edges = set()
    for seg in tri.geoms:
        (x1, y1), (x2, y2) = seg.coords
        a, b = index[(x1, y1)], index[(x2, y2)]
        if a > b:
            a, b = b, a
        edges.add((a, b))
    lengths = {e: math.dist(pts[e[0]], pts[e[1]]) for e in edges}
    # Gabriel filter: an edge survives only when no third city lies inside the
    # circle that has the edge as its diameter - the Delaunay graph is too
    # dense (six links a city) for a map where chokepoints should matter.
    arr = np.array(pts)
    gabriel = set()
    for a, b in edges:
        mx, my = (pts[a][0] + pts[b][0]) / 2, (pts[a][1] + pts[b][1]) / 2
        r2 = (lengths[(a, b)] / 2) ** 2
        d2 = (arr[:, 0] - mx) ** 2 + (arr[:, 1] - my) ** 2
        d2[a] = d2[b] = r2 + 1
        if not np.any(d2 < r2 - 1e-9):
            gabriel.add((a, b))
    keep = {e for e in gabriel if lengths[e] <= LAND_LINK}
    dsu = DSU(len(pts))
    for e in keep:
        dsu.union(*e)
    for e in sorted(edges, key=lambda e: (lengths[e], e)):
        if dsu.union(*e):
            keep.add(e)
    # antimeridian: pairs whose wrapped distance is short enough
    east = [i for i, p in enumerate(pts) if p[0] > MAP_WIDTH - 300]
    west = [i for i, p in enumerate(pts) if p[0] < 300]
    wraps = set()
    for i in east:
        for j in west:
            dx = MAP_WIDTH - (pts[i][0] - pts[j][0])
            dy = pts[i][1] - pts[j][1]
            if math.hypot(dx, dy) <= WRAP_LINK:
                e = (min(i, j), max(i, j))
                wraps.add(e)
                keep.add(e)
                lengths[e] = math.hypot(dx, dy)
    land = prep(land_union)
    links = []
    for a, b in sorted(keep):
        if (a, b) in wraps:
            sea = True
        else:
            seg = LineString([pts[a], pts[b]])
            inland = seg.intersection(land_union).length if not land.contains(seg) else seg.length
            sea = inland < 0.8 * seg.length
        links.append({"a": a, "b": b, "sea": sea, "wrap": (a, b) in wraps,
                      "km": round(lengths[(a, b)] * 40075.0 / MAP_WIDTH *
                                  math.cos(math.radians(
                                      (cities[a]["lat"] + cities[b]["lat"]) / 2)))})
    return links


# --- territories ------------------------------------------------------------
def build_territories(countries, cities, proj_polys):
    pts = [Point(c["x"], c["y"]) for c in cities]
    env = box(-50, -50, MAP_WIDTH + 50, MAP_HEIGHT + 50)
    cells = voronoi_diagram(MultiPoint(pts), envelope=env)
    cell_of = [None] * len(cities)
    tree = shapely.STRtree(pts)
    for cell in cells.geoms:
        cand = tree.query(cell, predicate="contains")
        for i in cand:
            cell_of[i] = cell
    territories = [Polygon()] * len(cities)
    by_iso = defaultdict(list)
    for i, c in enumerate(cities):
        by_iso[c["iso"]].append(i)
    for iso, idxs in by_iso.items():
        country = proj_polys[iso]
        pieces = []
        for i in idxs:
            t = polys_only(cell_of[i].intersection(country))
            territories[i] = t
            pieces.append(t)
        leftover = polys_only(country.difference(unary_union(pieces)))
        if leftover.is_empty:
            continue
        parts = list(leftover.geoms) if isinstance(leftover, MultiPolygon) else [leftover]
        for part in parts:
            if part.area < 0.05:
                continue
            best = min(idxs, key=lambda i: (part.distance(pts[i]), i))
            territories[best] = polys_only(unary_union([territories[best], part]))
    return territories


def assign_colours(countries, proj_polys, cities, links):
    """Greedy hue assignment, biggest country first: every neighbour - a
    country whose polygon touches, or one reached by any link - gets the hue
    farthest from the hues already taken around it."""
    isos = sorted(countries, key=lambda k: (-countries[k]["pop"], k))
    touching = defaultdict(set)
    prepared = {iso: prep(proj_polys[iso].buffer(1.5)) for iso in isos}
    for i, a in enumerate(isos):
        for b in isos[i + 1:]:
            if prepared[a].intersects(proj_polys[b]):
                touching[a].add(b)
                touching[b].add(a)
    for l in links:
        a, b = cities[l["a"]]["iso"], cities[l["b"]]["iso"]
        if a != b:
            touching[a].add(b)
            touching[b].add(a)
    hue_of = {}
    for iso in isos:
        used = [hue_of[n] for n in touching[iso] if n in hue_of]

        def score(h):
            return min((min(abs(h - u), 360 - abs(h - u)) for u in used), default=360)

        best = max(PALETTE, key=lambda h: (score(h), -h))
        hue_of[iso] = best
    colours = {}
    for i, iso in enumerate(isos):
        light = 0.52 if i % 2 == 0 else 0.42
        colours[iso] = hsl_to_hex(hue_of[iso], 0.58, light)
    return colours, touching


# --- main -------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetch", action="store_true", help="download missing inputs")
    ap.add_argument("--stats", action="store_true", help="print statistics")
    args = ap.parse_args()
    if args.fetch:
        fetch_all()
    for name in SOURCES:
        if not (RAW / name).exists():
            print(f"missing {RAW / name}; run with --fetch", file=sys.stderr)
            return 2

    countries, by_adm = load_countries()
    gdp = load_gdp()
    places = load_places(countries, by_adm)
    countries = {iso: c for iso, c in countries.items() if places.get(iso)}
    print(f"countries: {len(countries)}", flush=True)

    clip = box(-180, LAT_MIN, 180, LAT_MAX)
    proj_polys = {}
    for iso, c in countries.items():
        g = project_geom(c["geom"].intersection(clip))
        g = polys_only(g.simplify(SIMPLIFY_TOL, preserve_topology=True))
        proj_polys[iso] = g
    land_union = unary_union(list(proj_polys.values()))

    cities, quota = choose_cities(countries, places)
    print(f"cities: {len(cities)}", flush=True)
    links = build_links(cities, land_union)
    print(f"links: {len(links)}", flush=True)
    territories = build_territories(countries, cities, proj_polys)
    colours, touching = assign_colours(countries, proj_polys, cities, links)

    # keys
    keys = {}
    for i, c in enumerate(cities):
        base = f"{c['iso']}-{slug(c['name'])}"
        k, n = base, 2
        while k in keys.values():
            k = f"{base}-{n}"
            n += 1
        keys[i] = k

    capital_of = {}
    for i, c in enumerate(cities):
        if c["capital"]:
            capital_of[c["iso"]] = i

    out_countries = []
    for iso in sorted(countries):
        c = countries[iso]
        wb = WB_ISO3.get(c["adm0"], c["adm0"])
        if wb in gdp:
            gdp_usd, gdp_year = gdp[wb][1], gdp[wb][0]
        else:
            gdp_usd, gdp_year = c["gdp_md"] * 1e6, 0
        out_countries.append({
            "key": iso, "name": c["name"], "short": c["short"],
            "iso3": c["adm0"], "continent": c["continent"],
            "population": int(c["pop"]), "gdp": int(gdp_usd), "gdpYear": gdp_year,
            "color": colours[iso], "capital": capital_of[iso],
            "sovereign": (c["sovereign"] or "").lower(),
        })
    out_cities = []
    for i, c in enumerate(cities):
        out_cities.append({
            "id": i, "key": keys[i], "name": c["name"], "country": c["iso"],
            "lon": round(c["lon"], 4), "lat": round(c["lat"], 4),
            "x": round(c["x"], 1), "y": round(c["y"], 1),
            "population": c["pop"], "tier": tier_of(c["pop"]),
            "capital": c["capital"],
        })
    payload = {
        "version": 1,
        "projection": {"type": "miller", "width": round(MAP_WIDTH, 1),
                        "height": round(MAP_HEIGHT, 1),
                        "latMin": LAT_MIN, "latMax": LAT_MAX},
        "sources": sorted(SOURCES),
        "countries": out_countries, "cities": out_cities, "links": links,
    }
    canon = json.dumps({k: payload[k] for k in ("countries", "cities", "links")},
                       sort_keys=True, separators=(",", ":"))
    payload["hash"] = hashlib.sha256(canon.encode()).hexdigest()

    OUT.mkdir(parents=True, exist_ok=True)
    with open(OUT / "world.json", "w") as f:
        json.dump(payload, f, indent=1, ensure_ascii=False)
        f.write("\n")
    with open(OUT / "territories.json", "w") as f:
        json.dump({keys[i]: geom_to_path(t) for i, t in enumerate(territories)},
                  f, ensure_ascii=False, separators=(",", ":"))
        f.write("\n")
    with open(OUT / "outlines.json", "w") as f:
        json.dump({iso: geom_to_path(proj_polys[iso]) for iso in sorted(countries)},
                  f, ensure_ascii=False, separators=(",", ":"))
        f.write("\n")

    if args.stats:
        per = defaultdict(int)
        for c in cities:
            per[c["iso"]] += 1
        top = sorted(per.items(), key=lambda t: -t[1])[:12]
        print("top counts:", top)
        print("one-city countries:", sum(1 for v in per.values() if v == 1))
        print("sea links:", sum(1 for l in links if l["sea"]),
              "wrap:", sum(1 for l in links if l["wrap"]))
        empty = [keys[i] for i, t in enumerate(territories) if t.is_empty]
        print("empty territories:", empty[:20], len(empty))
        for name in ("world.json", "territories.json", "outlines.json"):
            print(name, (OUT / name).stat().st_size // 1024, "KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
