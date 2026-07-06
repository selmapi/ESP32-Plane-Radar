# CLAUDE.md — Plane Radar fork (selmapi/ESP32-Plane-Radar)

Live ADS-B desk radar: ESP32-C3 SuperMini + 1.28" GC9A01 (240x240, NON-touch),
forked from MatixYo/ESP32-Plane-Radar (MIT). V1 (themes/trails/phone app) and
V2 (Silent Running + CIC scope, region map, geometry fix, terminal app)
shipped 2026-07. The device lives on Selma's desk and is usually connected to
this Mac over USB.

## Working agreement (Selma)

- Sonnet implements, Opus reviews, the main session orchestrates only. Every
  diff gets an independent review before merge — no size exemptions.
- **Warn Selma before flashing** — a reflash once interrupted her mid-portal
  setup and cost her the WiFi config. Never flash while she's testing.
- She field-tests aggressively and gives design verdicts fast; treat her
  on-device feedback as authoritative over spec text, and log each decision
  as its own commit.
- Design docs in docs/superpowers/specs/, plans in docs/superpowers/plans/.

## Build / test / flash

```bash
pio run -e supermini              # build (pre-script regenerates webapp_gz.h)
pio run -e supermini -t upload    # flash over USB (device: /dev/cu.usbmodem*)
pio test -e native                # 47 tests (grows with features — trust `pio test -e native` output over this number); MUST stay green
pio test -e native -f "native/test_foo"   # NOTE: full nested name, or SKIPPED
```

- Serial monitor: 115200. For scripted reads use PlatformIO's bundled python
  (`/opt/homebrew/Cellar/platformio/*/libexec/bin/python3`) with pyserial.
- The merged flashable image lands at .pio/build/supermini/firmware-merged.bin
  (post-build script; also `pio run -t merge -e supermini`).

## Landmines (each cost us a debugging round — do not re-learn)

1. **LovyanGFX is pinned to exactly 1.2.21.** 1.2.24+ defines a `fonts` alias
   that collides with ours; anything near 1.2.7 predates two-arg loadFont().
   Upstream CI only stayed green via a stale cache. Never "upgrade" the pin.
2. **Panel color order:** the GC9A01 renders R/B swapped. ALL color packing
   goes through `ui::radar::themeColor565` (include/ui/theme_color.h) — the
   single home of the swap idiom. Theme table values are LOGICAL RGB except
   Midnight (index 0), which stores the R/B pre-swapped stock constants so it
   renders byte-identical to upstream (see comment in theme_table_data.cpp).
3. **Single-copy invariants** (grep before adding helpers): `lerpRgb565` only
   in include/ui/color_blend.h (Arduino-free — keep it that way for native
   tests); RGB swap only in theme_color.h; geometry only in
   include/ui/geo_transform.h (cos(lat) on longitude — the flat model was a
   24% E-W error at this latitude).
4. **Theme struct is positional aggregate init** (18 fields, 7 entries) with
   a sizeof static_assert + value-anchor tests. Adding a field = update the
   assert formula AND the anchors deliberately.
5. **[env:native]** uses test_build_src=yes + build_src_filter scoped to
   theme_table_data.cpp only. New native-linked .cpp files must be added to
   the filter; Arduino-bound code must NOT be.
6. **Generated files are committed and MUST be byte-stable**: webapp_gz.h
   (gzip mtime=0 — pre-3.14 Pythons default to wall-clock and dirty the repo)
   and region_map.h/region_map_data.cpp (regenerating from cache must produce
   an empty git diff; if it doesn't, that's a bug). The gzip header's OS byte
   is also pinned to 0xFF in scripts/build_webapp.py, since Python 3.14
   changed its default (0x03 -> 0xFF) and that alone made the header
   non-byte-stable across Python versions.
7. **Overpass API**: 406 = User-Agent WAF (the script's UA header is
   load-bearing); mirror fallback + scripts/cache/ make reruns offline.
   Cache keys do NOT include the query text — bust manually if a query changes.
8. **Privacy (history was scrubbed twice already — this is a repeat offender)**:
   the script defaults point at open ocean and the committed demo map is
   centered on Denver, CO — a neutral, well-known location with zero
   geographic connection to Selma's actual area. NEVER write Selma's real
   coordinates, home city, or address into ANY file this repo tracks —
   source, docs, plans, commit messages, **or this file**. Her real lat/lon
   is known to the assistant from conversation context and may live in the
   assistant's *local, non-repo* memory system for continuity — never in a
   git-tracked file. If a future session doesn't have it, ask Selma again;
   do not guess, and do not write it down anywhere this repo can see.

   **What went wrong twice:**
   - *Incident 1:* the map generator's committed output was built with
     Selma's exact home coordinates and shipped to the public repo (fixed
     with a full git-filter-repo history rewrite — expensive, avoid a repeat).
   - *Incident 2 (subtler):* after the scrub, `include/ui/region_map.h` /
     `src/data/region_map_data.cpp` in the repo held the PUBLIC demo center
     (first a public landmark, then Denver). Every ordinary
     `pio run -e supermini -t upload` — including unrelated fixes (theme
     colors, a callsign-color patch) — silently reflashed Selma's physical
     device with that public map, quietly drifting her on-device map away
     from her real house. Nobody noticed until she did. The lesson: the
     working tree has exactly ONE region-map dataset at a time, and it
     defaults to the public one — a routine "fix a bug, reflash" cycle will
     overwrite Selma's personal map with it unless you actively intervene.

   **The rule going forward:** before running `pio run -e supermini -t upload`
   against Selma's physical device, run `git status --short
   include/ui/region_map.h src/data/region_map_data.cpp`. If it's clean
   (matches committed/public data), STOP and ask whether this flash should
   carry Selma's personal map or the public one — don't assume. If she wants
   her personal map, use this exact sequence and do not skip the last step:

   ```bash
   # 1. Regenerate LOCALLY with her real coords (ask her, or use local memory
   #    if this session already has them) — never echoed into a repo file:
   python3 scripts/build_region_map.py --lat <REAL_LAT> --lon <REAL_LON> --radius 80

   # 2. Build + flash her device (warn her first, per the working-agreement rule):
   pio run -e supermini -t upload

   # 3. IMMEDIATELY restore the working tree to the public committed state —
   #    do not leave her real-coordinate data sitting in tracked files, even
   #    uncommitted, where a later `git add -A` could sweep it in:
   git checkout -- include/ui/region_map.h src/data/region_map_data.cpp
   git status --short   # must show nothing for these two files before moving on
   ```

   Her local `scripts/cache/*.json` files are named with her exact
   coordinates (e.g. `<lat>_<lon>_<radius>_<layer>.json`) — that's fine, the
   directory is gitignored and never leaves her Mac, but don't screenshot or
   paste those filenames anywhere public either.
9. **Button bands**: tap <1s = range, 1-8s release = theme, >=8s = WiFi wipe.
   The 8s was widened from 3s after a field accident — don't shrink it.
10. **Web handlers run synchronously on the main loop** (WiFiManager
    handleClient) — no races with selectionTick, by design. Don't add tasks.

## Architecture in one breath

adsb.fi polled every 3s (String-buffered — streaming parse is the known limit
for >50km ranges) → Aircraft[64] → full-frame sprite renderer
(radar_display.cpp; initPalette per frame from theme_manager; trails,
decorations, selection, CIC scope chrome + region map all layer in) → GC9A01.
WiFiManager's WebServer also serves the gzipped phone SPA (webapp/index.html)
+ JSON API (web_app.cpp: /api/aircraft, /api/select, /api/settings incl.
lat/lon, /api/status). Selection is ephemeral (30s after last phone poll).
CIC (theme 6, ScopeStyle::kCic) is the only theme with the map + brackets +
bearing ring; targets there are amber by owner decision.

## Deferred backlog (logged, not forgotten)

- Trails slot-handle refactor (per-point findSlot rescan; harmless at 0.33fps)
- Streaming ADS-B parse to unlock 100km+ ranges safely
- Terrain contours (v3 candidate; big data pipeline)
- Screenshot/GIF placeholders in README (docs/img/) are unfilled
- Upstream PRs (LovyanGFX pin, cos-lat fix) — MatixYo #49/#50, check status
- **Upstream PR rule**: this fork's history was rewritten (privacy scrub) and
  shares NO commit SHAs with upstream. Branches destined for upstream PRs must
  be created from upstream/main directly — never from fork main, or GitHub
  will diff the entire tree.
