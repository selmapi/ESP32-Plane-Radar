# Plane Radar V2 "Make It Cool" — Design Spec

**Date:** 2026-07-04
**Branch:** feature/v2-make-it-cool (off main @ ef7bf4e, V1 merged)
**Owner decisions:** all items below approved by Selma via mockup review on 2026-07-04.

## Goal

V1 made it capable; V2 makes it cool: two new radar personalities, a real regional map under the sweep, a terminal-styled phone app that matches the radar's mood, and docs so strangers can flash our fork.

## Non-goals

- Terrain/elevation contours (deferred to v3 — heavy data pipeline, weak payoff at 240 px)
- Long range beyond 50 km / streaming ADS-B parse (deferred; revisit if Selma wants 100 km+)
- Map overlay in themes other than CIC (deliberate first-ship constraint: evaluate busy-ness in one theme before spreading)

## Features

### 1. Theme roster change: "Silent Running" replaces "The Meatball"

Submarine night-vision red, index 5. Palette: bg near-black red `#0A0002`, rings `#7A1408`, crosshairs `#5A0F06`, labels/tags `#FF5A3A`, mono **brightness ramp** on base `#FF3A1E` (dim low → bright high, like Phosphor/Amber), **sweep enabled** (slow, same mechanism as Phosphor), decoration `kNone` (no swoosh — clutter-free is the point). NVS theme index 5 simply re-skins; users on old Meatball wake up in Silent Running.

### 2. New theme #7: "CIC" (vector scope)

Green-on-black combat-information-center scope, appended as index 6 (`kThemeCount` → 7). Distinctives beyond palette (bg `#020604`, ring `#2AAB5A`, targets `#5AFF8A`, brightness ramp, sweep enabled):
- **Bearing ring**: degree labels every 45° (000/045/.../315) inside the outer ring, monospace, replacing N/S/E/W cardinals in this theme only
- **Tick ring**: minor ticks every 15° on the outer ring
- **Faint square grid** backdrop (spacing ~30 px), drawn under everything
- **Bracket targets**: aircraft drawn as `[ · ]` corner brackets + center dot instead of the heading triangle (heading shown by the existing track vector line)
- These scope elements are driven by a new `ScopeStyle` flag on Theme (only CIC sets it), so other themes are untouched.

### 3. Region map overlay (CIC-only for now)

- **Data**: build-time generator `scripts/build_region_map.py` — fetches OSM data (Overpass API) for a configurable center/radius (default: Selma's home coordinates already in NVS/portal, radius 80 km), simplifies (Douglas-Peucker, ~150 m tolerance), and emits `src/data/region_map_data.cpp` (committed, like the airports data). A cached raw-response file avoids re-hitting Overpass on every regen.
- **Layers** (all four, individually toggleable in code, all-on by default): major highways (motorway/trunk/primary), rivers + lake outlines, county boundaries (dashed), town markers + ≤4-char labels (GSO, HP, KVL, PILOT...).
- **Rendering**: polylines drawn with the standard lat/lon→screen transform (so the map zooms with range presets automatically), clipped to the outer ring, in dim theme-derived colors, under runways/planes. Drawn only when the active theme is CIC.
- **Budget**: target ≤40 KB flash for vertex data; if the generator output exceeds it, raise simplification tolerance until it fits and report the tradeoff.
- **Relocation**: if the radar moves, rerun the generator with new coordinates and reflash. Documented in README. (Runtime lat/lon changes still work for planes; the baked map just stays put — a one-line note in the phone app settings explains this.)

### 4. Geometry correctness: cos(latitude) fix (prerequisite for the map)

The shared flat model (111 km/deg on both axes) overstates E-W distances by ~24% at 36°N — invisible with abstract blips, obvious with real roads. Fix the transform ONCE in the shared path: longitude deltas scale by `cosf(center_lat)`. Applies to: `offsetKmFromCenter`/`latLonToScreen` (radar_display.cpp), runway overlay (verify it uses the shared transform), `distanceKm` (web_app.cpp), fetch radius (unchanged — adsb.fi takes a radius, slight over-fetch is fine). Plane positions, runways, map, and phone distances all shift consistently. Native unit test: a point 1° east at 36°N maps to ~89.8 km, not 111 km. **This is a user-visible correction**: planes will render slightly closer E-W than before (correctly so). Candidate upstream PR (together with the LovyanGFX pin).

### 5. Range preset: +50 km

`kRangePresets` gains `{50, 66.7}`. Button cycle and phone app pick it up automatically (RANGES array in webapp gains "50").

### 6. Phone app: theme-reactive terminal redesign

Full restyle of webapp/index.html (single file, self-contained, same API):
- **Terminal aesthetic**: monospace type, scanline overlay (CSS), bordered panels, `►`-style markers, ALL-CAPS labels, subtle CRT glow (box-shadow inset)
- **Theme-reactive palette**: a CSS-variable palette per theme (6 entries… 7 with CIC) keyed off `state.theme` from `/api/aircraft` — the app re-tints live when the device theme changes (from any source: swatch tap, BOOT hold)
- **Mini-radar swatches**: each theme swatch becomes a tiny inline SVG radar (bg disc + ring + 2 blips in that theme's palette) instead of a flat color square
- **All v1 functionality preserved**: list, dossier (photo/route), select/deselect, settings (theme/range incl. 50 km/miles/runways/lat+lon), stale banner
- Fixture-testable as before; gz size budget ≤8 KB (raw ≤25 KB).

### 7. README for fork users

Rewrite README.md (fork-scoped, links upstream for credit + hardware/wiring):
- What this fork adds over upstream (feature list + photos/GIF placeholders)
- Flash instructions: releases path (merged bin at 0x0 via web.esphome.io) AND source path (PlatformIO build/upload; note the LovyanGFX 1.2.21 pin and why)
- First-boot setup (portal, lat/lon), phone app usage (plane-radar.local), button gestures (tap/1s/8s)
- Region map regeneration for a different home location (build_region_map.py usage)
- API reference table (the 5 endpoints)
- Credits: MatixYo (upstream, MIT), Capsule Radar (inspiration), planespotters/adsbdb/adsb.fi terms (non-commercial, attribution)

### 8. Debt paydown (from V1 final review)

- Consolidate the two byte-identical `lerpRgb565` copies into `include/ui/color_blend.h`
- Delete the dead `kBgR..kRunwayLabelB` block in radar_theme.h
- `/api/aircraft`: `out.reserve()` before serialization (cheap heap-margin win; full streaming deferred)

## Error handling & constraints

- Map generator: Overpass failures → clear error + retry hint; the committed data file means builds never depend on network.
- CIC scope elements + map add draw calls but no new RAM (all flash/const); frame budget unaffected at 0.33 fps.
- cos(lat) uses the runtime center latitude (recomputed when location changes) — not baked.
- Theme count change (6→7) must not break NVS: stored index 6 is now valid; stored ≥7 clamps to 0 (existing clamp logic covers it). Phone THEMES/SW arrays extended.

## Testing

- Native: cos(lat) transform test, theme table integrity updated (7 themes, CIC flags, Silent Running values), existing 27 stay green.
- Fixture: webapp visually testable against fixture as before.
- On-desk: Selma judges CIC map busy-ness (the point of the CIC-only constraint), Silent Running at night, app reactivity when cycling themes from the button.

## Suggested implementation order

1. Debt paydown + cos(lat) fix (foundation — everything after draws correctly)
2. Theme roster: Silent Running swap + CIC palette/scope styles + 50 km preset
3. Region map generator + renderer
4. Phone app terminal redesign
5. README + release polish
