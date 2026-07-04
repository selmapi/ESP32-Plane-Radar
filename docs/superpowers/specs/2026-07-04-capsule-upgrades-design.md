# Plane Radar "Capsule Upgrades" — Design Spec

**Date:** 2026-07-04
**Repo:** selmapi/ESP32-Plane-Radar (public fork of MatixYo/ESP32-Plane-Radar, MIT)
**Hardware:** ESP32-C3 SuperMini (AITRIP, 4 MB flash, no PSRAM) + 1.28″ GC9A01 240×240 round IPS (non-touch), wired per upstream README (RST=GPIO0, CS=1, MOSI=3, SCK=4, DC=10; BOOT button=GPIO9). No hardware changes; no additional soldering.

## Goal

Bring Capsule-Radar-inspired features to the existing desk radar, using the phone browser as the touch surface the hardware lacks. The device remains a fully standalone glanceable display; the phone page is an optional companion opened on demand.

## Non-goals

- Touch, audio, battery, IMU, GPS (hardware absent)
- OTA updates (no OTA partition in the 4 MB table; flashing stays USB)
- Authentication on the LAN API (conscious choice: LAN-only desk toy, same trust model as the existing WiFiManager portal)
- Changing the existing per-plane display (icon + callsign/type tag + altitude stays for every aircraft, always)

## Feature set

1. **Six switchable themes** — Midnight (stock look), Phosphor (green CRT + sweep wedge), Amber CRT, Vice (neon pink/violet), Mission Control (navy/gold braid/starfield), The Meatball (NASA blue/white rings/red swoosh decoration). Persisted in NVS.
2. **Altitude color-coding** — hue ramp on color themes (low=warm red → mid=amber → high=cyan/gold per theme table); brightness ramp on mono themes (Phosphor, Amber). Applies to aircraft icons; tags keep theme tag colors.
3. **Trails** — last ~8 positions per aircraft drawn as fading dots. ~5 KB RAM, fixed allocation.
4. **Sweep animation** — rotating sweep wedge, enabled per theme as a compile-time flag in the theme table: on for Phosphor, off for the other five.
5. **Phone companion page** at `http://plane-radar.local` — live plane list (sorted by distance), tap-to-select, dossier (photo, route, airline, alt, gs, v/s, squawk, distance, heading), settings drawer (theme, range, units, runways).
6. **Selection highlight + on-device card** — selected plane gets highlight ring; compact card (callsign, type, alt, speed) at bottom of round screen. Selection is ephemeral: cleared on phone deselect, plane loss, or ~30 s with no phone polls. All other planes keep their normal tags regardless of selection state.
7. **Button gestures** — short tap = range cycle (unchanged), ~1 s hold (fires on release) = next theme, ≥3 s hold = factory reset (unchanged).
8. **Emergency halo** — squawk 7500/7600/7700 → flashing halo on that aircraft (small freebie; can be dropped in review).

## Architecture

### Firmware (device)

New/changed modules, keeping fork rebaseable (new code in new files where possible):

- `ui/theme_manager` (new) — `struct Theme { name; 12-ish RGB565 targets; altitude ramp mode; decoration id; }`, `const Theme kThemes[6]`, active index in NVS (`planeradar` namespace, alongside existing `rangeIdx`). Replaces the constexpr palette application in `radar_theme.h`/`radar_display.cpp` `initPalette()` with a theme-driven one; the existing `kColor*` globals remain the renderer's interface, so draw code changes stay minimal.
- `services/adsb_client` (extend) — `Aircraft` gains `hex[7]`, `alt_ft` (numeric; keep formatted `alt[12]` for tags), `vs_fpm`, `squawk[5]`, `emergency` flag. All parsed from fields adsb.fi already returns. 64-aircraft cap unchanged.
- `ui/trails` (new) — fixed ring buffer keyed by hex: 64 aircraft × 8 points × lat/lon floats (8 B/point) ≈ 4 KB; stored as coordinates (not screen pixels) so trails stay correct across range changes; aged-out planes recycle slots; fade by age.
- `ui/decorations` (new) — sweep wedge, starfield, meatball swoosh; called by renderer based on active theme.
- `ui/selection` (new) — selected hex + last-phone-poll timestamp; highlight ring + bottom card draw; auto-clear logic.
- `services/web_app` (new) — registers routes on the existing WebServer instance that WiFiManager keeps running post-connect:
  - `GET /` → gzipped single-page app from flash (PROGMEM asset, ~15 KB gz), served alongside existing portal routes (portal remains at its current paths)
  - `GET /api/aircraft` → JSON snapshot: device lat/lon, range, units, theme, selected hex, aircraft array (hex, callsign, type, alt_ft, gs, vs_fpm, squawk, emergency, lat, lon, track, distance). Serialized into a static ~4 KB buffer, chunked if needed.
  - `POST /api/select` → body `{"hex":"a1b2c3"}` or `{"hex":null}`
  - `POST /api/settings` → subset of `{theme, rangeIdx, useMiles, showRunways}`; applies immediately, persists via existing save paths
  - `GET /api/status` → version, uptime, RSSI, heap
- `main.cpp` (small edits) — medium-hold gesture dispatch; selection timeout tick.

### Phone mini-app

One `webapp/index.html` source in-repo (vanilla JS + inline CSS, no framework, dark UI). Build step (extend existing `scripts/`) gzips it into a C array header at compile time. Behavior:

- Polls `/api/aircraft` every 3 s; renders list + radar-state header; staleness banner if two polls fail.
- Tap row → `POST /api/select`, expands dossier.
- Dossier fetches, from the **phone's** connection (never through the ESP32):
  - Photo: `https://api.planespotters.net/pub/photos/hex/{hex}` → thumbnail + **photographer credit displayed** (API terms).
  - Route: `https://api.adsbdb.com/v0/callsign/{callsign}` → origin → destination; cached in-page per callsign.
- Settings drawer → `POST /api/settings` with theme swatches matching the six palettes.
- Testable standalone in a desktop browser against a canned JSON fixture (`webapp/fixtures/aircraft.json`).

## Data flow

adsb.fi → (HTTPS, every 3 s, unchanged cadence, 1 req/s limit respected) → ESP32 aircraft array → (a) renderer each frame, (b) `/api/aircraft` JSON on phone poll. Phone → planespotters/adsbdb directly for photo/route. Phone → `/api/select`, `/api/settings` → device state → NVS where persistent.

**Persistence:** theme, range, units, runways, WiFi credentials, lat/lon survive reboot (NVS). Selection is deliberately volatile.

## Error handling & constraints

- **RAM:** no PSRAM; biggest allocation stays the existing 240×240 sprite (112.5 KB) with existing direct-draw fallback. New fixed allocations ≈ 9–10 KB total (trails + JSON buffer). No dynamic per-request heap churn beyond WebServer's own.
- **adsb.fi outage:** keep rendering last data; stale indicator on device after ~15 s without a successful fetch; phone shows staleness banner. No crash, no blank screen.
- **planespotters/adsbdb outage:** phone dossier shows "—" placeholders; device unaffected.
- **mDNS unavailable:** page reachable by IP (device already prints/serves LAN IP; status screen shows it).
- **Upstream compatibility:** fork stays rebaseable on MatixYo main; upstream pulls via `upstream` remote.

## Testing

- PlatformIO build locally (arm64 Mac; note local .venv is x86_64 — PlatformIO manages its own toolchain, no dependency on that venv); flash over USB (device already connected).
- Inherited GitHub Actions CI build on every PR to the fork.
- Per-feature PRs on `feature/*` branches into fork `main`.
- Mini-app: desktop-browser test against fixtures before device integration.
- Acceptance on-desk: all six themes cycled (button and phone), altitude colors sane against known traffic, select/deselect from phone, selection auto-clear ~30 s after closing page, plug-pull persistence of theme/range/units, stock plane tags present in all states.

## Implementation order (suggested for planning)

1. Aircraft struct extension + parse (foundation for everything)
2. Theme manager + six palettes + button gesture (visible win, no web dependency)
3. Altitude color-coding + trails + decorations + emergency halo
4. Web app + API + selection/card
5. Polish: staleness indicators, acceptance pass
