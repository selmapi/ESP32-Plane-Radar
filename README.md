# Plane Radar (V2 fork)

A live ADS-B radar for an **ESP32-C3 Super Mini** + a **1.28" round GC9A01**
display (240x240, non-touch). Shows aircraft around your location on a
sonar-style scope, with a WiFiManager captive portal for setup and a
theme-reactive phone companion app served straight off the device.

This is a fork of [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar)
(MIT). See upstream for the **3D-printed case (STL + assembly)** and **wiring**.

## What this fork adds

- **7 radar themes**, cycled from the BOOT button or the phone app, persisted
  to flash: Midnight, Phosphor (green CRT + sweep), Amber CRT, Vice (neon),
  Mission Control (navy/gold + starfield), **Silent Running** (submarine
  night-vision red), and **CIC** (combat-information-center vector scope).
- **Altitude color-coding** on aircraft (warm low -> cool/bright high).
- **Fading position trails** behind moving aircraft.
- **CIC scope mode**: pure black background, green instrument chrome (a
  bearing ring labeled 000/045/.../315, minor ticks every 15°, and a faint
  square grid), **amber** bracket targets/tags for maximum contrast against
  the chrome and map layers, and a **real regional map** (interstates, rivers
  and lakes, county lines, town markers) baked from OpenStreetMap and drawn in
  natural cartographic colors (gray-white roads, blue water, gray county
  lines, warm-white towns). No sweep in this mode.
- **Corrected geometry**: longitude distances now scale by cos(latitude), so
  roads, runways, and plane positions are E-W accurate (not stretched ~24% at
  mid latitudes).
- **Range presets** 5 / 10 / 15 / 25 / **50** km.
- **Phone companion app** at `plane-radar.local`: terminal-styled UI whose
  palette re-tints live to match the device's active theme, aircraft list,
  per-aircraft dossier (photo + route), select/deselect, and settings
  (theme via mini-radar swatches, range, units, runways, and lat/lon editing).

<!-- Screenshots / GIF placeholders:
  docs/img/themes.gif        -- cycling all 7 themes
  docs/img/cic-scope.jpg     -- CIC scope with region map
  docs/img/phone-app.jpg     -- terminal phone app
-->

## Flash it

### Path A -- prebuilt release image (no toolchain)

1. Download the merged `.bin` from **Releases**.
2. Open [web.esphome.io](https://web.esphome.io/) (or esptool-js) in Chrome/Edge.
3. Put the board in download mode (hold **BOOT**, tap **RESET**), connect over
   USB-C, and flash the image at offset **0x0** (ESP32-C3, 4 MB). There is no
   OTA partition -- every update is a USB reflash.

### Path B -- build from source (PlatformIO)

```bash
pio run -e supermini -t upload
pio device monitor -e supermini    # 115200 baud
```

> **LovyanGFX is pinned to exactly `1.2.21`** in `platformio.ini`. Do not bump
> it: `1.2.24+` adds a global `fonts` alias that collides with this project's,
> and `^1.2.7` (or anything before `1.2.21`) predates the two-arg
> `loadFont()` this code uses. This fork also relies on a hand-rolled
> `lerpRgb565` (see `include/ui/color_blend.h`) because that pin has no
> `alphaBlend` helper.

There is no OTA partition configured, so both paths above end in a USB flash
-- either the merged `.bin` at offset 0x0, or `pio run -e supermini -t upload`.

## First boot

1. On first power-up (no saved Wi-Fi) the device starts an AP named
   **`PlaneRadar-Setup`**.
2. Join it, then open **`http://plane-radar.local`** (or `http://192.168.4.1`).
3. Set your home **Wi-Fi**, **latitude/longitude**, units, and runway overlay;
   save. The device reconnects and starts the radar.

Reconfigure later at **`http://plane-radar.local`** (or the device IP) while it
is on your network.

## Button gestures (BOOT, GPIO 9)

| Action | Effect |
|--------|--------|
| **Tap** (< 1 s) | Cycle range preset (5 -> 10 -> 15 -> 25 -> 50 km); saved |
| **Hold ~1 s** then release | Cycle theme (7 themes); saved |
| **Hold 8 s** | Clear Wi-Fi + location + units; reboot into the setup portal |

The 8 s reset is deliberate so an overshot theme-hold never wipes your Wi-Fi.
(`kBootThemeHoldMs` = 1000 ms, `kBootResetHoldMs` = 8000 ms in
`include/config.h`.)

## Phone app

Browse **`http://plane-radar.local`** on the same network. The app is a
terminal-styled UI that re-tints live to match whichever theme is active on
the device, polls every 3 s, shows a stale banner if the device stops
responding, lists aircraft by distance, and expands a dossier per aircraft
(photo via planespotters, route via adsbdb). The **CFG** drawer sets theme
(mini-radar swatches showing each theme's palette), range (incl. 50 km),
miles/km, runways, and location (latitude/longitude fields).

**Note:** changing location moves the planes, but the **baked CIC map stays
put** -- regenerate + reflash to move the map (below).

## Region map (CIC theme)

The CIC map is baked at build time from OpenStreetMap for a fixed center and
radius: interstate-class roads only (`motorway` -- trunk/primary are fetched
but excluded from the scope by design, since they read as noise at radar
scale; e.g. I-40/I-74/I-77/I-85 in the default region), rivers/lakes, county
lines, and town markers. The committed example data is centered on downtown
Winston-Salem, NC (`36.0999, -80.2442`, 80 km radius) purely as a demo region.
The script's built-in defaults deliberately point at open ocean, so **always
pass your own coordinates** when rebuilding:

```bash
python3 scripts/build_region_map.py --lat <LAT> --lon <LON> --radius 80
pio run -e supermini -t upload
```

Privacy tip: use a nearby public landmark (downtown, an airport) rather than
your exact address — the map looks identical at radar scale, and the center
coordinates are committed to the repo.

The generator fetches roads/water/boundaries/towns via the Overpass API
(trying a primary endpoint, then a public mirror, per layer), sends a
`User-Agent` header as required by Overpass usage policy, caches raw
responses under `scripts/cache/` (gitignored, so reruns for the same
location/radius are free and work offline), simplifies the geometry, and
emits `src/data/region_map_data.cpp` (committed). It enforces a **96 KB**
flash budget via a deterministic simplification ladder and prints per-layer
byte/segment telemetry (warning above 5,000 segments — the real constraint is
per-frame draw cost, not flash). Overpass rate-limits; if a fetch fails, wait
~30 s and re-run (the cache makes repeats free).

To restore trunk/primary roads instead of interstates-only, edit the
`classes = ["motorway"]` line in `scripts/build_region_map.py` -- see the
comment directly above it (`# Owner decision (2026-07-04): interstates only on
the scope...`) for how to bring the other classes back.

## Settings reference

| Setting | What it does | Where to change it | Survives reboot? |
|---------|--------------|--------------------|------------------|
| Wi-Fi credentials | Network the radar joins | Setup portal only (first boot, or after an 8 s BOOT reset) | Yes |
| Latitude / Longitude | Center of the radar (what traffic is fetched/shown) | Setup portal, or phone app **CFG** drawer (Save location) | Yes |
| Theme (7) | Full display restyle | BOOT hold ~1 s, or phone app swatches | Yes |
| Range (5/10/15/25/50 km) | Scope radius; map + rings zoom together | BOOT tap, or phone app **CFG** drawer | Yes |
| Units (km / miles) | Distance display on device + app | Setup portal, or phone app **CFG** drawer | Yes |
| Runway overlay | Major-airport runways on the scope | Setup portal, or phone app **CFG** drawer | Yes |
| Selected aircraft | Highlight ring + info card on device | Tap a plane in the phone app (tap again to clear) | No — auto-clears ~30 s after the app closes |
| Factory reset | Wipes Wi-Fi + location + units, reopens setup portal | Hold BOOT 8 s | — |

Note: changing latitude/longitude moves the *planes* immediately; the CIC
*map* is baked at build time and stays put until you regenerate + reflash
(see below).

## Phone / device API

Served on the device's web server (same host as the setup portal). See
`src/services/web_app.cpp` for the handlers.

| Method + path | Purpose |
|---------------|---------|
| `GET /` | The phone companion app (gzipped) |
| `GET /api/aircraft` | Snapshot: state (lat/lon, theme, range, units, runways, selected) + aircraft list with distance |
| `POST /api/select` | `{"hex":"a1b2c3"}` to highlight on-device; `{"hex":null}` to clear |
| `POST /api/settings` | `{theme, rangeIdx, useMiles, showRunways, lat, lon}` (any subset) |
| `GET /api/status` | `{uptime_s, rssi, heap, ip}` |

## Build a release image

```bash
pio run -e supermini
pio run -t merge -e supermini      # -> .pio/build/supermini/firmware-merged.bin
```

Flash the merged image at **0x0**.

Build uses PlatformIO (env `supermini`); resource use on that env is roughly
**17.5% RAM** and **41% flash**. The native unit-test suite (`pio test -e
native`, 44 tests) covers theme data, geometry, and map logic without
touching hardware.

## Credits

- Upstream firmware & hardware: **[MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar)** (MIT).
- Scope aesthetic inspired by **Capsule Radar** (by socquique).
- ADS-B data: **[adsb.fi](https://opendata.adsb.fi/)** (free tier, ~1 request/s courtesy limit, non-commercial use, attribution).
- Aircraft photos: **[planespotters.net](https://www.planespotters.net/)** (photographer credit shown per photo, required by their API terms; non-commercial use).
- Routes: **[adsbdb](https://www.adsbdb.com/)** (attribution).
- Map data: **© [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors**, [ODbL](https://opendatacommons.org/licenses/odbl/), fetched via the Overpass API.
- Libraries: [LovyanGFX](https://github.com/lovyan03/LovyanGFX), [WiFiManager](https://github.com/tzapu/WiFiManager), [ArduinoJson](https://github.com/bblanchon/ArduinoJson).
