#!/usr/bin/env python3
"""Generate the SVG icon of every unit type (docs/DATA_PIPELINE.md §7).

Source of truth: assets/units/units.json. One 64x64 SVG per unit is written
to assets/units/icons/<key>.svg plus a review page assets/units/preview.html.

Style contract:
  * viewBox 0 0 64 64, white (#ffffff) stroke-based glyphs on a transparent
    background - the client draws the banner-coloured disc underneath.
  * Modern military silhouettes read at 24 px: a rifle, a reticle, a tube,
    a barrel on wheels, hulls with turrets, twin AA barrels, a rotor, a
    rocket box, a delta wing.
  * A soft glow (feGaussianBlur filter) and a brushed gradient on hulls
    exercise the SVG features Qt 6.7+ renders; renderers without filter
    support simply skip the glow.
  * No <text>, no external references. Deterministic: pure stdlib.
Usage: python3 scripts/gen_unit_svgs.py
"""

from __future__ import annotations

import json
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UNITS = REPO / "assets" / "units"
ICONS = UNITS / "icons"

DEFS = """<defs>
  <linearGradient id="hull" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ffffff" stop-opacity="0.55"/>
    <stop offset="1" stop-color="#ffffff" stop-opacity="0.15"/>
  </linearGradient>
  <filter id="glow" x="-20%" y="-20%" width="140%" height="140%">
    <feGaussianBlur stdDeviation="1.2" result="b"/>
    <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
</defs>"""

S = 'fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"'
F = 'fill="url(#hull)" stroke="#ffffff" stroke-width="2.5" stroke-linejoin="round"'


def wheel(cx, cy, r=4.5):
    return f'<circle cx="{cx}" cy="{cy}" r="{r}" {F}/><circle cx="{cx}" cy="{cy}" r="1.5" fill="#ffffff"/>'


GLYPHS = {
    # helmeted rifleman: head, shoulders, rifle across the chest
    "soldier": [
        f'<circle cx="26" cy="17" r="7" {F}/>',
        f'<path d="M16 46 Q16 30 26 28 Q36 30 36 46 Z" {F}/>',
        f'<path d="M18 40 L52 18" {S}/>',
        f'<path d="M46 22 L50 28 M40 26 L43 31" {S}/>',
    ],
    # reticle over a long barrel
    "sniper": [
        f'<circle cx="26" cy="26" r="13" {S}/>',
        f'<path d="M26 8 L26 17 M26 35 L26 44 M8 26 L17 26 M35 26 L44 26" {S}/>',
        f'<circle cx="26" cy="26" r="2.5" fill="#ffffff"/>',
        f'<path d="M36 40 L58 58" {S} stroke-width="4"/>',
        f'<path d="M46 44 L52 44" {S}/>',
    ],
    # shoulder tube with a rocket leaving it
    "rocketlauncher": [
        f'<path d="M10 44 L42 20" {S} stroke-width="7"/>',
        f'<path d="M10 44 L42 20" stroke="#ffffff" stroke-opacity="0.35" stroke-width="2" fill="none"/>',
        f'<path d="M44 18 L56 9 L52 21 Z" {F}/>',
        f'<path d="M14 52 Q20 46 26 50 Q30 42 36 46" {S} stroke-opacity="0.7"/>',
        f'<circle cx="18" cy="24" r="5" {F}/>',
    ],
    # howitzer: wheel, trail, long barrel at 30 degrees
    "artillery": [
        wheel(22, 46, 8),
        f'<path d="M22 46 L8 54" {S}/>',
        f'<path d="M22 46 L54 14" {S} stroke-width="5"/>',
        f'<path d="M48 20 L54 26" {S}/>',
        f'<path d="M30 34 L38 42" {S} stroke-opacity="0.6"/>',
    ],
    # wheeled hull, three wheels, small turret
    "afv": [
        f'<path d="M8 40 L14 28 L50 28 L58 40 Z" {F}/>',
        f'<path d="M26 28 L28 20 L40 20 L42 28" {F}/>',
        f'<path d="M40 22 L54 18" {S}/>',
        wheel(16, 44), wheel(32, 44), wheel(48, 44),
    ],
    # tracked hull, turret, long barrel
    "tank": [
        f'<rect x="8" y="38" width="48" height="12" rx="6" {F}/>',
        f'<path d="M14 38 L18 28 L46 28 L50 38" {F}/>',
        f'<path d="M24 28 L26 20 L40 20 L42 28" {F}/>',
        f'<path d="M40 23 L60 19" {S} stroke-width="4"/>',
        '<circle cx="16" cy="44" r="2" fill="#ffffff"/><circle cx="26" cy="44" r="2" fill="#ffffff"/>'
        '<circle cx="36" cy="44" r="2" fill="#ffffff"/><circle cx="46" cy="44" r="2" fill="#ffffff"/>',
    ],
    # twin barrels pointing up, radar dish
    "antiaircraft": [
        f'<rect x="10" y="40" width="44" height="12" rx="4" {F}/>',
        f'<path d="M24 40 L26 30 L38 30 L40 40" {F}/>',
        f'<path d="M28 30 L22 8 M36 30 L42 8" {S}/>',
        f'<path d="M18 10 L26 8 M38 8 L46 10" {S}/>',
        f'<path d="M48 30 A8 8 0 1 1 48 14" {S}/>',
        f'<path d="M48 22 L44 34" {S}/>',
    ],
    # gunship: fuselage, rotor, tail boom and fin
    "attackhelicopter": [
        f'<path d="M14 34 Q14 24 26 24 L40 24 Q48 24 48 32 Q48 40 40 40 L24 40 Q14 40 14 34 Z" {F}/>',
        f'<path d="M6 21 L58 21" {S}/>',
        f'<path d="M32 24 L32 18" {S}/>',
        f'<path d="M48 32 L60 30 L60 22" {S}/>',
        f'<path d="M20 46 L44 46 M22 40 L22 46 M42 40 L42 46" {S}/>',
        f'<path d="M12 36 L6 42 M6 36 L12 42" {S} stroke-width="2"/>',
    ],
    # truck with an angled box of rocket tubes
    "mlrs": [
        f'<rect x="8" y="42" width="48" height="10" rx="4" {F}/>',
        f'<path d="M10 42 L10 30 L22 30 L22 42" {F}/>',
        f'<path d="M24 40 L52 16 L60 24 L32 46 Z" {F}/>',
        f'<path d="M30 36 L36 42 M36 30 L42 36 M42 24 L48 30" {S} stroke-width="2"/>',
        wheel(18, 50, 3.5), wheel(36, 50, 3.5), wheel(50, 50, 3.5),
    ],
    # angular heavy tank with skirts and a stabilised barrel
    "modernarmor": [
        f'<path d="M6 40 L10 32 L54 32 L58 40 L54 50 L10 50 Z" {F}/>',
        f'<path d="M20 32 L24 22 L44 22 L48 32" {F}/>',
        f'<path d="M44 26 L62 24" {S} stroke-width="5"/>',
        f'<path d="M12 41 L52 41" stroke="#ffffff" stroke-opacity="0.5" stroke-width="2" fill="none"/>',
        f'<path d="M30 22 L34 16" {S} stroke-width="2"/>',
    ],
    # delta-wing jet seen from above
    "fighter": [
        f'<path d="M32 6 L38 26 L58 46 L40 44 L36 54 L28 54 L24 44 L6 46 L26 26 Z" {F}/>',
        f'<path d="M32 12 L32 52" {S} stroke-width="2" stroke-opacity="0.6"/>',
        f'<path d="M28 54 L26 60 M36 54 L38 60" {S}/>',
        f'<circle cx="32" cy="20" r="3" fill="#ffffff"/>',
    ],
}


def svg_for(key: str) -> str:
    body = "\n  ".join(GLYPHS[key])
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">\n'
        f"{DEFS}\n"
        f'<g filter="url(#glow)">\n  {body}\n</g>\n</svg>\n'
    )


def main() -> int:
    units = json.load(open(UNITS / "units.json"))["units"]
    ICONS.mkdir(parents=True, exist_ok=True)
    cards = []
    for u in units:
        key = u["key"]
        if key not in GLYPHS:
            raise SystemExit(f"no glyph for {key}")
        (ICONS / f"{key}.svg").write_text(svg_for(key))
        cards.append(
            f'<figure><div class="dark"><img src="icons/{key}.svg"></div>'
            f'<div class="light"><img src="icons/{key}.svg"></div>'
            f'<figcaption>{u["name"]}<br><small>{u["cost"]} · {" ".join(u["classes"])}</small></figcaption></figure>')
    (UNITS / "preview.html").write_text(
        "<!doctype html><meta charset=utf-8><title>Unit icons</title>"
        "<style>body{font:14px sans-serif;background:#1b2230;color:#e6e1d6;margin:24px}"
        "main{display:grid;grid-template-columns:repeat(4,1fr);gap:18px}"
        "figure{margin:0;text-align:center}img{width:96px;height:96px}"
        ".dark,.light{display:inline-block;border-radius:50%;padding:8px;margin:4px}"
        ".dark{background:#b03a3a}.light{background:#d3ad55}</style>"
        "<h1>Altered Domination — unit icons</h1><main>" + "".join(cards) + "</main>\n")
    print(f"wrote {len(units)} icons")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
