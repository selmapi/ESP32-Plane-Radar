#!/usr/bin/env python3
"""Build a compact regional basemap for the CIC scope from OpenStreetMap.

Fetches major roads, rivers/lakes, county boundaries, and town markers within a
radius of a center via the Overpass API; stitches OSM's fragmented ways into
chains; filters water noise; simplifies polylines with Douglas-Peucker;
quantizes vertices to int16 offsets (0.0001 deg units) from the center; and
emits src/data/region_map_data.cpp + include/ui/region_map.h.

Usage:
  python3 scripts/build_region_map.py [--lat 36.0999] [--lon -80.2442]
                                      [--radius 80]
The device's baked map is fixed to this center; runtime lat/lon changes still
move the planes, but the map stays put (rerun + reflash to relocate).
"""

from __future__ import annotations

import argparse
import json
import math
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "scripts" / "cache"
OUT_H = ROOT / "include" / "ui" / "region_map.h"
OUT_CPP = ROOT / "src" / "data" / "region_map_data.cpp"

# Primary endpoint first, then a public mirror; tried in order per layer.
OVERPASS_URLS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]

# Required: overpass-api.de's WAF 406-blocks requests with default library
# User-Agent strings (e.g. Python-urllib) -- do not remove this header.
USER_AGENT = "PlaneRadarMapGen/1.0 (+https://github.com/selmapi/ESP32-Plane-Radar)"

DEFAULT_LAT = 36.0999
DEFAULT_LON = -80.2442
DEFAULT_RADIUS_KM = 80.0

# int16 offset quantization: 1 unit = 1e-4 deg. Range +/-32767 units = +/-3.27
# deg ~= +/-360 km N-S, comfortably beyond an 80 km radius.
QUANT_DEG = 1e-4

# 96 KB budget: flash has ~1.88 MB free, so the original 40 KB spec cap was
# arbitrary; the real limit is per-frame draw cost, which the segment-count
# telemetry below watches instead (warn above SEGMENT_WARN).
FLASH_BUDGET_BYTES = 96 * 1024

# Water noise filters (radar scale): keep stitched river chains only if the
# total length is >= 5 km; keep lake/pond outlines only if the bbox diagonal
# is >= 2 km. Smaller features are invisible at an 80 km radius display.
RIVER_MIN_KM = 5.0
LAKE_MIN_DIAG_KM = 2.0

# Draw-time telemetry: warn (not fail) if the emitted segment count exceeds
# this -- large counts threaten per-frame draw budgets, not flash.
SEGMENT_WARN = 5000

# Deterministic budget ladder: (DP tolerance meters, include primary roads).
# Replaces the old unbounded tolerance loop, which could never converge --
# the 12 B/polyline span+endpoint floor does not shrink with tolerance.
LADDER = [(150.0, True), (300.0, True), (300.0, False)]

# Layer ids (must match the enum order in region_map.h).
LAYER_HIGHWAY = 0
LAYER_WATER = 1
LAYER_BOUNDARY = 2
LAYER_TOWN = 3
LAYER_NAMES = ["Highway", "Water", "Boundary", "Town"]

HIGHWAY_CLASSES = ["motorway", "trunk", "primary"]


def overpass_query(lat: float, lon: float, radius_m: int, kind: str) -> str:
    """Overpass QL for one layer, around(radius, lat, lon)."""
    around = f"(around:{radius_m},{lat},{lon})"
    if kind == "highway":
        return (
            "[out:json][timeout:120];"
            "("
            f'way["highway"~"^(motorway|trunk|primary)$"]{around};'
            ");out geom;"
        )
    if kind == "water":
        return (
            "[out:json][timeout:120];"
            "("
            f'way["waterway"="river"]{around};'
            f'way["natural"="water"]{around};'
            ");out geom;"
        )
    if kind == "boundary":
        # County lines are relations, not tagged ways: fetch the member ways
        # of admin_level=6 relations (way(r)) with geometry.
        return (
            "[out:json][timeout:120];"
            f'relation["boundary"="administrative"]["admin_level"="6"]{around};'
            "way(r);"
            "out geom;"
        )
    if kind == "town":
        return (
            "[out:json][timeout:120];"
            "("
            f'node["place"~"^(town|city)$"]{around};'
            ");out;"
        )
    raise ValueError(kind)


def fetch(lat: float, lon: float, radius_m: int, kind: str) -> dict:
    """Fetch one Overpass layer, caching the raw JSON under scripts/cache/."""
    CACHE.mkdir(parents=True, exist_ok=True)
    key = f"{lat:.5f}_{lon:.5f}_{radius_m}_{kind}.json"
    cache_file = CACHE / key
    if cache_file.exists():
        return json.loads(cache_file.read_text(encoding="utf-8"))
    query = overpass_query(lat, lon, radius_m, kind)
    data = urllib.parse.urlencode({"data": query}).encode("utf-8")
    last_err: Exception | None = None
    raw = None
    for url in OVERPASS_URLS:
        req = urllib.request.Request(
            url, data=data, headers={"User-Agent": USER_AGENT}
        )
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                raw = r.read().decode("utf-8")
            break
        except urllib.error.URLError as e:
            last_err = e
    if raw is None:
        raise SystemExit(
            f"Overpass fetch failed for '{kind}' on all endpoints "
            f"({', '.join(OVERPASS_URLS)}): {last_err}. "
            "Retry (the API rate-limits; wait ~30 s), or run offline once the "
            "cache under scripts/cache/ is populated."
        )
    cache_file.write_text(raw, encoding="utf-8")
    time.sleep(2)  # be polite to Overpass between layer fetches
    return json.loads(raw)


def ways_geometry(payload: dict) -> list[list[tuple[float, float]]]:
    """Extract each way's [(lat, lon), ...] geometry."""
    lines: list[list[tuple[float, float]]] = []
    for el in payload.get("elements", []):
        geom = el.get("geometry")
        if not geom:
            continue
        line = [(pt["lat"], pt["lon"]) for pt in geom]
        if len(line) >= 2:
            lines.append(line)
    return lines


def highway_lines_by_class(payload: dict) -> dict:
    """Split highway ways by class so the ladder can drop primary roads."""
    out: dict = {c: [] for c in HIGHWAY_CLASSES}
    for el in payload.get("elements", []):
        geom = el.get("geometry")
        if not geom:
            continue
        cls = (el.get("tags") or {}).get("highway", "")
        if cls not in out:
            continue
        line = [(pt["lat"], pt["lon"]) for pt in geom]
        if len(line) >= 2:
            out[cls].append(line)
    return out


def water_lines(payload: dict) -> tuple[list, list]:
    """Split water ways into river centerlines and lake/pond outlines."""
    rivers: list = []
    lakes: list = []
    for el in payload.get("elements", []):
        geom = el.get("geometry")
        if not geom:
            continue
        line = [(pt["lat"], pt["lon"]) for pt in geom]
        if len(line) < 2:
            continue
        tags = el.get("tags") or {}
        if tags.get("waterway") == "river":
            rivers.append(line)
        elif tags.get("natural") == "water":
            lakes.append(line)
    return rivers, lakes


def town_nodes(payload: dict) -> list[tuple[float, float, str]]:
    """Extract (lat, lon, <=4-char label) for town/city nodes."""
    towns: list[tuple[float, float, str]] = []
    for el in payload.get("elements", []):
        if el.get("type") != "node":
            continue
        name = (el.get("tags", {}) or {}).get("name", "")
        if not name:
            continue
        # Uppercase initials if multi-word, else first 4 chars uppercased.
        parts = name.split()
        if len(parts) >= 2:
            label = "".join(p[0] for p in parts)[:4].upper()
        else:
            label = name[:4].upper()
        towns.append((el["lat"], el["lon"], label))
    return towns


def _ekey(pt: tuple[float, float]) -> tuple[int, int]:
    """Quantized endpoint key (~1e-4 deg) for stitch matching."""
    return (round(pt[0] / QUANT_DEG), round(pt[1] / QUANT_DEG))


def stitch_lines(lines: list) -> list:
    """Merge ways sharing (quantized) endpoints into chains.

    OSM fragments roads/rivers into many short ways; each costs a 12 B
    span+endpoint floor in the emitted data. Joining fragments into real
    chains is what makes the flash budget reachable. Closed rings pass
    through unchanged.
    """
    pool: dict = {i: list(l) for i, l in enumerate(lines)}
    while True:
        endmap: dict = {}
        for i, l in pool.items():
            k0, k1 = _ekey(l[0]), _ekey(l[-1])
            if k0 == k1:
                continue  # closed ring: leave as-is
            endmap.setdefault(k0, []).append(i)
            endmap.setdefault(k1, []).append(i)
        joined = False
        for k, ids in endmap.items():
            live = [i for i in ids if i in pool]
            if len(live) < 2:
                continue
            a, b = live[0], live[1]
            if a == b:
                continue
            la, lb = pool[a], pool[b]
            # Orient so la ends at k and lb starts at k (reverse as needed).
            if _ekey(la[0]) == k:
                la = la[::-1]
            if _ekey(lb[-1]) == k:
                lb = lb[::-1]
            if _ekey(la[-1]) != k or _ekey(lb[0]) != k:
                continue  # stale entry from an earlier join this pass
            pool[a] = la + lb[1:]
            del pool[b]
            joined = True
        if not joined:
            return list(pool.values())


def line_len_km(line: list) -> float:
    """Approximate polyline length in km (planar, cos-lat corrected)."""
    total = 0.0
    for a, b in zip(line, line[1:]):
        dlat = (b[0] - a[0]) * 111.0
        dlon = (b[1] - a[1]) * 111.0 * math.cos(math.radians(a[0]))
        total += math.hypot(dlat, dlon)
    return total


def bbox_diag_km(line: list) -> float:
    """Approximate bbox diagonal in km (planar, cos-lat corrected)."""
    lats = [p[0] for p in line]
    lons = [p[1] for p in line]
    dlat = (max(lats) - min(lats)) * 111.0
    dlon = (max(lons) - min(lons)) * 111.0 * math.cos(math.radians(lats[0]))
    return math.hypot(dlat, dlon)


def perp_dist(pt, a, b) -> float:
    """Perpendicular distance of pt from segment a-b (degrees, planar)."""
    (py, px), (ay, ax), (by, bx) = pt, a, b
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    projx, projy = ax + t * dx, ay + t * dy
    return math.hypot(px - projx, py - projy)


def douglas_peucker(line, tol_deg: float):
    """Iterative Douglas-Peucker simplification (tolerance in degrees).

    Iterative (explicit stack) because stitched chains can run to thousands
    of vertices -- recursion would overflow Python's stack.
    """
    n = len(line)
    if n < 3:
        return list(line)
    keep = [False] * n
    keep[0] = keep[-1] = True
    stack = [(0, n - 1)]
    while stack:
        a, b = stack.pop()
        if b - a < 2:
            continue
        dmax, idx = 0.0, a
        for i in range(a + 1, b):
            d = perp_dist(line[i], line[a], line[b])
            if d > dmax:
                dmax, idx = d, i
        if dmax > tol_deg:
            keep[idx] = True
            stack.append((a, idx))
            stack.append((idx, b))
    return [p for p, k in zip(line, keep) if k]


def quantize(value_deg: float, center_deg: float) -> int:
    """int16 offset units (1e-4 deg) from center, clamped to int16 range."""
    q = int(round((value_deg - center_deg) / QUANT_DEG))
    return max(-32768, min(32767, q))


def build_layers(lat, lon, radius_m):
    """Fetch, stitch, and filter all layers."""
    hw_by_class = highway_lines_by_class(fetch(lat, lon, radius_m, "highway"))
    rivers, lakes = water_lines(fetch(lat, lon, radius_m, "water"))
    boundary = ways_geometry(fetch(lat, lon, radius_m, "boundary"))
    towns = town_nodes(fetch(lat, lon, radius_m, "town"))

    hw_by_class = {c: stitch_lines(v) for c, v in hw_by_class.items()}
    rivers = [
        l for l in stitch_lines(rivers) if line_len_km(l) >= RIVER_MIN_KM
    ]
    lakes = [
        l for l in stitch_lines(lakes) if bbox_diag_km(l) >= LAKE_MIN_DIAG_KM
    ]
    boundary = stitch_lines(boundary)
    return hw_by_class, rivers + lakes, boundary, towns


def encode(highway, water, boundary, lat, lon, tol_deg):
    """Simplify + quantize each layer; return (verts, spans_by_layer)."""
    verts: list[tuple[int, int]] = []
    spans: dict[int, list[tuple[int, int]]] = {
        LAYER_HIGHWAY: [],
        LAYER_WATER: [],
        LAYER_BOUNDARY: [],
    }
    for layer_id, lines in (
        (LAYER_HIGHWAY, highway),
        (LAYER_WATER, water),
        (LAYER_BOUNDARY, boundary),
    ):
        for line in lines:
            simp = douglas_peucker(line, tol_deg)
            if len(simp) < 2:
                continue
            start = len(verts)
            for (py, px) in simp:
                verts.append((quantize(py, lat), quantize(px, lon)))
            spans[layer_id].append((start, len(simp)))
    return verts, spans


def vertex_bytes(vert_count: int, span_count: int) -> int:
    # 2x int16 per vertex (4 B) + 2x uint16 per span (4 B).
    return vert_count * 4 + span_count * 4


def render_header(vert_count, span_count, town_count, lat, lon) -> str:
    return "\n".join(
        [
            "// Generated by scripts/build_region_map.py -- do not edit.",
            "#pragma once",
            "",
            "#include <cstddef>",
            "#include <cstdint>",
            "",
            "namespace ui::radar {",
            "",
            "enum class MapLayer : uint8_t {",
            "  kHighway = 0,",
            "  kWater = 1,",
            "  kBoundary = 2,",
            "  kTown = 3,",
            "};",
            "",
            "/** One polyline: [start, start+len) into kMapVerts, tagged by layer. */",
            "struct MapSpan {",
            "  uint16_t start;",
            "  uint16_t len;",
            "  uint8_t layer;",
            "};",
            "",
            "/** int16 lat/lon offset from the baked center, in kMapQuantDeg units. */",
            "struct MapVert {",
            "  int16_t dlat;",
            "  int16_t dlon;",
            "};",
            "",
            "struct MapTown {",
            "  int16_t dlat;",
            "  int16_t dlon;",
            "  char label[5];",
            "};",
            "",
            "constexpr float kMapQuantDeg = 1e-4f;",
            f"constexpr float kMapCenterLat = {lat:.5f}f;",
            f"constexpr float kMapCenterLon = {lon:.5f}f;",
            f"constexpr size_t kMapVertCount = {vert_count};",
            f"constexpr size_t kMapSpanCount = {span_count};",
            f"constexpr size_t kMapTownCount = {town_count};",
            "",
            "extern const MapVert kMapVerts[];",
            "extern const MapSpan kMapSpans[];",
            "extern const MapTown kMapTowns[];",
            "",
            "}  // namespace ui::radar",
            "",
        ]
    )


def render_cpp(verts, spans, towns, lat, lon) -> str:
    lines = [
        "// Generated by scripts/build_region_map.py -- do not edit.",
        '#include "ui/region_map.h"',
        "",
        "namespace ui::radar {",
        "",
        "const MapVert kMapVerts[] = {",
    ]
    for i in range(0, len(verts), 8):
        chunk = ", ".join(f"{{{dy},{dx}}}" for (dy, dx) in verts[i:i + 8])
        lines.append(f"  {chunk},")
    lines += ["};", "", "const MapSpan kMapSpans[] = {"]
    ordered = (
        [(s, l, LAYER_HIGHWAY) for (s, l) in spans[LAYER_HIGHWAY]]
        + [(s, l, LAYER_WATER) for (s, l) in spans[LAYER_WATER]]
        + [(s, l, LAYER_BOUNDARY) for (s, l) in spans[LAYER_BOUNDARY]]
    )
    for (s, l, layer) in ordered:
        lines.append(f"  {{{s}, {l}, {layer}}},")
    lines += ["};", "", "const MapTown kMapTowns[] = {"]
    for (ty, tx, label) in towns:
        lines.append(
            f'  {{{quantize(ty, lat)}, {quantize(tx, lon)}, "{label}"}},'
        )
    lines += ["};", "", "}  // namespace ui::radar", ""]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lat", type=float, default=DEFAULT_LAT)
    ap.add_argument("--lon", type=float, default=DEFAULT_LON)
    ap.add_argument("--radius", type=float, default=DEFAULT_RADIUS_KM)
    args = ap.parse_args()

    radius_m = int(args.radius * 1000)
    hw_by_class, water, boundary, towns = build_layers(
        args.lat, args.lon, radius_m
    )

    # Deterministic budget ladder (see LADDER above). Hard-fails if even the
    # last rung is over budget -- never loops on a non-shrinking size.
    verts = spans = None
    span_count = total = 0
    rung_desc = ""
    for tol_m, use_primary in LADDER:
        classes = ["motorway", "trunk"] + (["primary"] if use_primary else [])
        highway = [l for c in classes for l in hw_by_class[c]]
        tol_deg = tol_m / 111000.0
        verts, spans = encode(
            highway, water, boundary, args.lat, args.lon, tol_deg
        )
        span_count = sum(len(v) for v in spans.values())
        total = vertex_bytes(len(verts), span_count)
        rung_desc = f"DP {tol_m:.0f} m" + (
            "" if use_primary else " + primary roads DROPPED"
        )
        if total <= FLASH_BUDGET_BYTES:
            break
        print(f"  over budget at {rung_desc}: {total} B")
    else:
        raise SystemExit(
            f"vertex data {total} B exceeds {FLASH_BUDGET_BYTES} B even at "
            "the final ladder rung; shrink --radius, relax the budget, or "
            "trim layers."
        )

    header = render_header(
        len(verts), span_count, len(towns), args.lat, args.lon
    )
    cpp = render_cpp(verts, spans, towns, args.lat, args.lon)
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_CPP.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(header, encoding="utf-8")
    OUT_CPP.write_text(cpp, encoding="utf-8")

    town_bytes = len(towns) * 9  # 2x int16 + 5-char label
    segments = len(verts) - span_count  # each span of len L draws L-1 segs
    print(
        f"wrote {OUT_H.name} + {OUT_CPP.name}\n"
        f"  center {args.lat:.5f},{args.lon:.5f}  radius {args.radius:.0f} km"
    )
    for lid in (LAYER_HIGHWAY, LAYER_WATER, LAYER_BOUNDARY):
        lv = sum(l for (_, l) in spans[lid])
        ls = len(spans[lid])
        print(
            f"  {LAYER_NAMES[lid]}: {lv} verts, {ls} spans, "
            f"{vertex_bytes(lv, ls)} B"
        )
    print(
        f"  {len(verts)} verts, {span_count} spans, {len(towns)} towns\n"
        f"  vertex data {total} B (budget {FLASH_BUDGET_BYTES} B), "
        f"towns {town_bytes} B, ladder rung: {rung_desc}\n"
        f"  segments {segments} (draw-time warn threshold {SEGMENT_WARN})"
    )
    if segments > SEGMENT_WARN:
        print(
            f"  WARNING: {segments} segments > {SEGMENT_WARN}; per-frame "
            "draw cost may be too high on the C3."
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
