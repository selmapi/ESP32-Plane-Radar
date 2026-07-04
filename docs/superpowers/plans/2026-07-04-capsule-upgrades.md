# Capsule Upgrades Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add six switchable themes, altitude color-coding, fading trails, per-theme decorations (sweep/starfield/swoosh), an emergency halo, a phone companion web app served from the ESP32, selection highlight + on-device info card, and a medium-hold theme-cycle button gesture to the standalone Plane Radar.

**Architecture:** New pure-logic modules (`altitude_ramp`, `theme_manager` tables, `selection` timeout logic, JSON field extraction) live in header-clean files so a PlatformIO `native` Unity test env can exercise them without Arduino headers; hardware-bound rendering/serving/gesture code is verified manually on device. The theme system keeps the existing `ui::radar::kColor*` globals as the renderer's interface — `initPalette()` is rewritten to copy from the active `Theme` in a `const kThemes[6]` table (persisted in the existing `planeradar` NVS namespace), so draw-code churn stays minimal. The phone app is a single self-contained `webapp/index.html` gzipped at build time into a PROGMEM header and served on WiFiManager's own `WebServer` instance (`wm.server->on(...)`), alongside a small JSON API.

**Tech Stack:** ESP32-C3 (Arduino framework, PlatformIO `espressif32@6.5.0`), LovyanGFX 1.2.7, WiFiManager 2.0.17, ArduinoJson 7.4.2, Preferences (NVS), Unity (via `pio test -e native`) for pure-logic TDD, Python 3 build helpers, vanilla HTML/CSS/JS for the phone app.

---

## File Structure

### Created

| Path | Responsibility |
| --- | --- |
| `include/ui/altitude_ramp.h` | Header-only pure functions: altitude → RGB ramp (color themes) and brightness ramp (mono themes); `native`-testable. |
| `include/ui/theme_manager.h` | `struct Theme`, `enum RampMode`, `enum DecorationId`, `const Theme kThemes[6]` extern decl, active-theme accessors. |
| `src/ui/theme_manager.cpp` | Defines `kThemes[6]` (RGB8 targets), loads/saves active index in `planeradar` NVS, `themeNext()`, applies theme into `kColor*` globals. |
| `include/ui/trails.h` | Trail ring-buffer API: `trailsUpdate(list,count)`, `trailsForHex(hex)`, point accessors. |
| `src/ui/trails.cpp` | Fixed 64×8 lat/lon ring buffer keyed by hex; slot recycle by age; fade-by-age. |
| `include/ui/decorations.h` | `drawSweep`, `drawStarfield`, `drawMeatballSwoosh`, dispatched by active theme's `DecorationId`. |
| `src/ui/decorations.cpp` | LovyanGFX draw routines for the per-theme decorations. |
| `include/ui/selection.h` | Selection state: set/clear selected hex, phone-poll timestamp, `selectionTick()` auto-clear, highlight + bottom card draw. |
| `src/ui/selection.cpp` | Selection state machine + draw; `native`-testable timeout split into `selection_logic.h`. |
| `include/ui/selection_logic.h` | Header-only pure timeout predicate (`selectionShouldClear`); `native`-testable. |
| `include/services/web_app.h` | `webAppRegisterRoutes(WebServer&)` + status accessors. |
| `src/services/web_app.cpp` | Registers `GET /`, `GET /api/aircraft`, `POST /api/select`, `POST /api/settings`, `GET /api/status` on WiFiManager's server; serializes the aircraft snapshot into a static buffer. |
| `include/web/webapp_gz.h` | Generated: gzipped `index.html` as a PROGMEM byte array (do not edit). |
| `webapp/index.html` | Phone companion single-page app (vanilla JS + inline CSS). |
| `webapp/fixtures/aircraft.json` | Canned `/api/aircraft` fixture for desktop-browser testing. |
| `scripts/build_webapp.py` | Gzips `webapp/index.html` → `include/web/webapp_gz.h`; wired as a PlatformIO pre-build `extra_script`. |
| `test/native/test_altitude_ramp/test_altitude_ramp.cpp` | Unity tests for the altitude ramp math. |
| `test/native/test_theme_table/test_theme_table.cpp` | Unity tests for theme-table integrity. |
| `test/native/test_json_extract/test_json_extract.cpp` | Unity tests for extracting the new fields into `Aircraft` (ArduinoJson on native). |
| `test/native/test_selection_logic/test_selection_logic.cpp` | Unity tests for the selection auto-clear predicate. |

### Modified

| Path | Change |
| --- | --- |
| `include/config.h` | Add `kBootThemeHoldMs` (medium-hold threshold) and squawk emergency constant helpers. |
| `include/services/adsb_client.h` | Extend `Aircraft` with `hex[7]`, `alt_ft`, `vs_fpm`, `squawk[5]`, `emergency`. |
| `src/services/adsb_client.cpp` | Parse the new fields from the adsb.fi `/v3` response. |
| `src/services/wifi_setup.cpp` | Add a medium-hold gesture latch to the BOOT ISR + `bootButtonConsumeThemeHold()`; register web-app routes via `setWebServerCallback`. |
| `include/services/wifi_setup.h` | Declare `bootButtonConsumeThemeHold()`. |
| `src/ui/radar_display.cpp` | Rewrite `initPalette()` to pull from the active theme; color aircraft by altitude; draw trails, decorations, emergency halo, selection highlight + bottom card. |
| `src/main.cpp` | Dispatch the medium-hold theme cycle; tick selection timeout each loop. |
| `platformio.ini` | Add `pre:scripts/build_webapp.py` to `extra_scripts`; add `[env:native]` Unity test env with ArduinoJson dep. |

---

## Task 1 — Extend the Aircraft struct + parse the new fields (TDD for parsing)

Foundation for altitude coloring, emergency halo, and the JSON API. The parsing loop change is pure-logic (ArduinoJson runs on native), so it is unit-tested first.

**Files:**
- Modify: `include/services/adsb_client.h` (lines 7–16, the `Aircraft` struct)
- Modify: `platformio.ini` (lines 5–27, add `[env:native]`)
- Create: `test/native/test_json_extract/test_json_extract.cpp`
- Modify: `src/services/adsb_client.cpp` (lines 190–198 `fillTagFields`, lines 268–274 the fill block; add new helpers)

- [ ] Extend the `Aircraft` struct. In `include/services/adsb_client.h`, replace the struct (lines 7–16) with:

```cpp
struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];    // formatted tag string ("12300 ft" / "GND"), unchanged
  char hex[7];     // ICAO 24-bit, lowercase hex, no leading '~'
  int32_t alt_ft;  // numeric barometric altitude in feet; INT32_MIN if unknown/ground
  int16_t vs_fpm;  // vertical rate, feet per minute; 0 if unknown
  char squawk[5];  // 4-digit transponder code, e.g. "7700"
  bool emergency;  // true when squawk is 7500/7600/7700
};
```

Also add `#include <cstdint>` at the top of the header (after `#include <cstddef>`).

- [ ] Add the `[env:native]` test env. Append to `platformio.ini`:

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
  -std=gnu++17
  -DUNIT_TEST
  -Iinclude
lib_deps =
  bblanchon/ArduinoJson@^7.4.2
```

- [ ] Write the failing JSON-extract test. Create `test/native/test_json_extract/test_json_extract.cpp`:

```cpp
#include <ArduinoJson.h>
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "services/aircraft_fields.h"

using services::adsb::Aircraft;
using services::adsb::extractExtendedFields;

static Aircraft parseOne(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  JsonObject plane = doc.as<JsonObject>();
  Aircraft ac{};
  extractExtendedFields(&ac, plane);
  return ac;
}

void test_hex_strips_tilde_and_lowercases() {
  Aircraft ac = parseOne(R"({"hex":"~A1B2C3"})");
  TEST_ASSERT_EQUAL_STRING("a1b2c3", ac.hex);
}

void test_alt_ft_numeric() {
  Aircraft ac = parseOne(R"({"alt_baro":12300})");
  TEST_ASSERT_EQUAL_INT32(12300, ac.alt_ft);
}

void test_alt_ft_ground_is_sentinel() {
  Aircraft ac = parseOne(R"({"alt_baro":"ground"})");
  TEST_ASSERT_EQUAL_INT32(INT32_MIN, ac.alt_ft);
}

void test_vs_prefers_baro_rate() {
  Aircraft ac = parseOne(R"({"baro_rate":-1200,"geom_rate":900})");
  TEST_ASSERT_EQUAL_INT16(-1200, ac.vs_fpm);
}

void test_vs_falls_back_to_geom_rate() {
  Aircraft ac = parseOne(R"({"geom_rate":900})");
  TEST_ASSERT_EQUAL_INT16(900, ac.vs_fpm);
}

void test_squawk_and_emergency_7700() {
  Aircraft ac = parseOne(R"({"squawk":"7700"})");
  TEST_ASSERT_EQUAL_STRING("7700", ac.squawk);
  TEST_ASSERT_TRUE(ac.emergency);
}

void test_squawk_normal_not_emergency() {
  Aircraft ac = parseOne(R"({"squawk":"1200"})");
  TEST_ASSERT_EQUAL_STRING("1200", ac.squawk);
  TEST_ASSERT_FALSE(ac.emergency);
}

void test_emergency_field_true() {
  Aircraft ac = parseOne(R"({"emergency":"general"})");
  TEST_ASSERT_TRUE(ac.emergency);
}

void test_emergency_field_none_is_false() {
  Aircraft ac = parseOne(R"({"emergency":"none"})");
  TEST_ASSERT_FALSE(ac.emergency);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hex_strips_tilde_and_lowercases);
  RUN_TEST(test_alt_ft_numeric);
  RUN_TEST(test_alt_ft_ground_is_sentinel);
  RUN_TEST(test_vs_prefers_baro_rate);
  RUN_TEST(test_vs_falls_back_to_geom_rate);
  RUN_TEST(test_squawk_and_emergency_7700);
  RUN_TEST(test_squawk_normal_not_emergency);
  RUN_TEST(test_emergency_field_true);
  RUN_TEST(test_emergency_field_none_is_false);
  return UNITY_END();
}
```

- [ ] Create the header-clean extraction module so both native tests and the firmware share one implementation. Create `include/services/aircraft_fields.h`:

```cpp
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "services/adsb_client.h"

namespace services::adsb {

/** Lowercase-hex copy of "hex", dropping a leading TIS-B '~'. */
inline void extractHex(Aircraft* ac, const JsonObject& plane) {
  ac->hex[0] = '\0';
  if (!plane["hex"].is<const char*>()) {
    return;
  }
  const char* s = plane["hex"].as<const char*>();
  if (s[0] == '~') {
    ++s;
  }
  size_t i = 0;
  for (; s[i] != '\0' && i < sizeof(ac->hex) - 1; ++i) {
    const char c = s[i];
    ac->hex[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  ac->hex[i] = '\0';
}

/** Numeric barometric altitude in feet; INT32_MIN for "ground" or missing. */
inline void extractAltFt(Aircraft* ac, const JsonObject& plane) {
  ac->alt_ft = INT32_MIN;
  if (plane["alt_baro"].is<const char*>()) {
    return;  // "ground"
  }
  if (plane["alt_baro"].is<int>() || plane["alt_baro"].is<float>()) {
    ac->alt_ft = plane["alt_baro"].as<int32_t>();
  } else if (plane["alt_geom"].is<int>() || plane["alt_geom"].is<float>()) {
    ac->alt_ft = plane["alt_geom"].as<int32_t>();
  }
}

/** Vertical rate: prefer baro_rate, else geom_rate; 0 if neither present. */
inline void extractVsFpm(Aircraft* ac, const JsonObject& plane) {
  ac->vs_fpm = 0;
  if (plane["baro_rate"].is<int>() || plane["baro_rate"].is<float>()) {
    ac->vs_fpm = plane["baro_rate"].as<int16_t>();
  } else if (plane["geom_rate"].is<int>() || plane["geom_rate"].is<float>()) {
    ac->vs_fpm = plane["geom_rate"].as<int16_t>();
  }
}

inline bool squawkIsEmergency(const char* sq) {
  return strcmp(sq, "7500") == 0 || strcmp(sq, "7600") == 0 ||
         strcmp(sq, "7700") == 0;
}

/** Squawk string + emergency flag (from squawk code or adsb.fi "emergency"). */
inline void extractSquawk(Aircraft* ac, const JsonObject& plane) {
  ac->squawk[0] = '\0';
  if (plane["squawk"].is<const char*>()) {
    const char* s = plane["squawk"].as<const char*>();
    strncpy(ac->squawk, s, sizeof(ac->squawk) - 1);
    ac->squawk[sizeof(ac->squawk) - 1] = '\0';
  }
  bool emergency = squawkIsEmergency(ac->squawk);
  if (plane["emergency"].is<const char*>()) {
    const char* e = plane["emergency"].as<const char*>();
    if (e[0] != '\0' && strcmp(e, "none") != 0) {
      emergency = true;
    }
  }
  ac->emergency = emergency;
}

/** Fill hex, alt_ft, vs_fpm, squawk, emergency. Existing tag fields untouched. */
inline void extractExtendedFields(Aircraft* ac, const JsonObject& plane) {
  extractHex(ac, plane);
  extractAltFt(ac, plane);
  extractVsFpm(ac, plane);
  extractSquawk(ac, plane);
}

}  // namespace services::adsb
```

- [ ] Run the test — expect FAIL (the firmware doesn't call `extractExtendedFields` yet, but the header compiles; this run proves the test env + module build). Command:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio test -e native -f test_json_extract
```

Expected output ends with a passing summary IF the header is already correct. Because this module is self-contained, this test should actually **PASS** on first run — that is acceptable (the header IS the implementation). If it FAILS, read the Unity assertion line, fix `aircraft_fields.h`, and re-run until:

```
test/native/test_json_extract/test_json_extract.cpp:... :PASS
-----------------------
9 Tests 0 Failures 0 Ignored
OK
```

- [ ] Wire the extraction into the firmware parse loop. In `src/services/adsb_client.cpp`, add the include after line 6 (`#include <ArduinoJson.h>`):

```cpp
#include "services/aircraft_fields.h"
```

Then in `fetchUpdate`, immediately after the existing `fillTagFields(&s_aircraft[n], plane);` call (line 273), add:

```cpp
    extractExtendedFields(&s_aircraft[n], plane);
```

- [ ] Build the firmware to confirm the struct + include compile on-target:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini
```

Expected: `SUCCESS` with a RAM/Flash summary (RAM use should rise by only a few hundred bytes: 64 × (7+4+2+4+1) ≈ 1.2 KB for the enlarged array).

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/services/adsb_client.h include/services/aircraft_fields.h src/services/adsb_client.cpp platformio.ini test/native/test_json_extract && git commit -m "Extend Aircraft with hex/alt_ft/vs_fpm/squawk/emergency + native test

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 2 — Altitude ramp math (pure, TDD)

Header-only ramp used later by the renderer. Kept Arduino-free so the `native` env compiles it.

**Files:**
- Create: `include/ui/altitude_ramp.h`
- Create: `test/native/test_altitude_ramp/test_altitude_ramp.cpp`

- [ ] Write the failing ramp test. Create `test/native/test_altitude_ramp/test_altitude_ramp.cpp`:

```cpp
#include <unity.h>

#include <cstdint>

#include "ui/altitude_ramp.h"

using ui::radar::Rgb8;
using ui::radar::rampColor;
using ui::radar::rampBrightness;

void test_low_altitude_is_first_stop() {
  const Rgb8 lo{0xFF, 0x4A, 0x2A};
  const Rgb8 mid{0xFF, 0xD2, 0x4A};
  const Rgb8 hi{0x39, 0xD0, 0xFF};
  Rgb8 c = rampColor(0, lo, mid, hi);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.r);
  TEST_ASSERT_EQUAL_UINT8(0x4A, c.g);
  TEST_ASSERT_EQUAL_UINT8(0x2A, c.b);
}

void test_high_altitude_clamps_to_last_stop() {
  const Rgb8 lo{0xFF, 0x4A, 0x2A};
  const Rgb8 mid{0xFF, 0xD2, 0x4A};
  const Rgb8 hi{0x39, 0xD0, 0xFF};
  Rgb8 c = rampColor(60000, lo, mid, hi);  // above kRampTopFt
  TEST_ASSERT_EQUAL_UINT8(0x39, c.r);
  TEST_ASSERT_EQUAL_UINT8(0xD0, c.g);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.b);
}

void test_mid_altitude_hits_middle_stop() {
  const Rgb8 lo{0, 0, 0};
  const Rgb8 mid{100, 100, 100};
  const Rgb8 hi{200, 200, 200};
  // kRampMidFt is the altitude that maps exactly to the mid stop.
  Rgb8 c = rampColor(ui::radar::kRampMidFt, lo, mid, hi);
  TEST_ASSERT_UINT8_WITHIN(2, 100, c.r);
  TEST_ASSERT_UINT8_WITHIN(2, 100, c.g);
  TEST_ASSERT_UINT8_WITHIN(2, 100, c.b);
}

void test_unknown_altitude_uses_low_stop() {
  const Rgb8 lo{0xFF, 0x4A, 0x2A};
  const Rgb8 mid{0xFF, 0xD2, 0x4A};
  const Rgb8 hi{0x39, 0xD0, 0xFF};
  Rgb8 c = rampColor(INT32_MIN, lo, mid, hi);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.r);
}

void test_brightness_scales_base_color() {
  const Rgb8 base{0x1E, 0x8A, 0x3C};
  Rgb8 lo = rampBrightness(0, base);
  Rgb8 hi = rampBrightness(60000, base);
  // Higher altitude -> brighter (each channel scaled up), clamped at 255.
  TEST_ASSERT_TRUE(hi.g >= lo.g);
  TEST_ASSERT_TRUE(lo.g > 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_low_altitude_is_first_stop);
  RUN_TEST(test_high_altitude_clamps_to_last_stop);
  RUN_TEST(test_mid_altitude_hits_middle_stop);
  RUN_TEST(test_unknown_altitude_uses_low_stop);
  RUN_TEST(test_brightness_scales_base_color);
  return UNITY_END();
}
```

- [ ] Run — expect FAIL (module does not exist yet):

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio test -e native -f test_altitude_ramp
```

Expected: a compile error `ui/altitude_ramp.h: No such file or directory`.

- [ ] Implement the ramp. Create `include/ui/altitude_ramp.h`:

```cpp
#pragma once

#include <cstdint>

namespace ui::radar {

struct Rgb8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

/** Altitude (ft) mapped to the middle ramp stop. */
constexpr int32_t kRampMidFt = 18000;
/** Altitude (ft) at/above which the ramp is fully at the top stop. */
constexpr int32_t kRampTopFt = 40000;

inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
  const float v = static_cast<float>(a) + (static_cast<float>(b) - a) * t;
  if (v <= 0.0f) return 0;
  if (v >= 255.0f) return 255;
  return static_cast<uint8_t>(v + 0.5f);
}

inline Rgb8 lerpRgb(const Rgb8& a, const Rgb8& b, float t) {
  return Rgb8{lerp8(a.r, b.r, t), lerp8(a.g, b.g, t), lerp8(a.b, b.b, t)};
}

/**
 * Three-stop altitude ramp. alt_ft == INT32_MIN (unknown/ground) -> low stop.
 * [0, kRampMidFt] interpolates low->mid; [kRampMidFt, kRampTopFt] mid->high;
 * clamped outside.
 */
inline Rgb8 rampColor(int32_t alt_ft, const Rgb8& low, const Rgb8& mid,
                      const Rgb8& high) {
  if (alt_ft == INT32_MIN || alt_ft <= 0) {
    return low;
  }
  if (alt_ft >= kRampTopFt) {
    return high;
  }
  if (alt_ft <= kRampMidFt) {
    const float t = static_cast<float>(alt_ft) / static_cast<float>(kRampMidFt);
    return lerpRgb(low, mid, t);
  }
  const float t = static_cast<float>(alt_ft - kRampMidFt) /
                  static_cast<float>(kRampTopFt - kRampMidFt);
  return lerpRgb(mid, high, t);
}

/**
 * Mono-theme brightness ramp: scale a base color from 45% (ground) to 100%
 * (>= kRampTopFt) of full intensity.
 */
inline Rgb8 rampBrightness(int32_t alt_ft, const Rgb8& base) {
  float frac;
  if (alt_ft == INT32_MIN || alt_ft <= 0) {
    frac = 0.45f;
  } else if (alt_ft >= kRampTopFt) {
    frac = 1.0f;
  } else {
    frac = 0.45f + 0.55f * (static_cast<float>(alt_ft) /
                            static_cast<float>(kRampTopFt));
  }
  return Rgb8{lerp8(0, base.r, frac), lerp8(0, base.g, frac),
              lerp8(0, base.b, frac)};
}

}  // namespace ui::radar
```

- [ ] Run — expect PASS:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio test -e native -f test_altitude_ramp
```

Expected:

```
5 Tests 0 Failures 0 Ignored
OK
```

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/ui/altitude_ramp.h test/native/test_altitude_ramp && git commit -m "Add pure altitude ramp math with native tests

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 3 — Theme table + theme manager (TDD for table integrity)

Defines the six palettes and the NVS-backed active index. `initPalette()` will read this table in Task 5. Table integrity (all 6 present, decoration/ramp enums valid, sweep only on Phosphor) is `native`-tested.

**Files:**
- Create: `include/ui/theme_manager.h`
- Create: `src/ui/theme_manager.cpp`
- Create: `include/ui/theme_table.h` (header-clean table data, shared with native tests)
- Create: `test/native/test_theme_table/test_theme_table.cpp`

- [ ] Create the header-clean theme table. Create `include/ui/theme_table.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/altitude_ramp.h"

namespace ui::radar {

enum class RampMode : uint8_t { kColor, kBrightness };
enum class DecorationId : uint8_t { kNone, kSweep, kStarfield, kMeatball };

/**
 * A theme is a set of RGB8 targets (converted to RGB565 in initPalette),
 * an altitude ramp mode + three ramp stops, and a decoration id.
 * `name` is short (fits the phone swatch label and on-device debug prints).
 */
struct Theme {
  const char* name;
  Rgb8 bg;
  Rgb8 grid;
  Rgb8 label;
  Rgb8 center;
  Rgb8 tag_type;
  Rgb8 tag_alt;
  Rgb8 track;
  Rgb8 runway;
  Rgb8 runway_label;
  Rgb8 decoration;   // sweep wedge / swoosh / starfield tint
  RampMode ramp_mode;
  Rgb8 ramp_low;
  Rgb8 ramp_mid;
  Rgb8 ramp_high;
  DecorationId decoration_id;
  bool sweep_enabled;
};

constexpr size_t kThemeCount = 6;

extern const Theme kThemes[kThemeCount];

}  // namespace ui::radar
```

- [ ] Create the table definition. Create `src/ui/theme_table_data.cpp`:

```cpp
#include "ui/theme_table.h"

namespace ui::radar {

// clang-format off
const Theme kThemes[kThemeCount] = {
    // 0 — Midnight (stock look; ramp red->amber->cyan)
    {
        "Midnight",
        {4, 10, 28},      // bg  (kBgR/G/B)
        {16, 100, 32},    // grid
        {255, 255, 255},  // label
        {255, 255, 255},  // center
        {255, 200, 0},    // tag_type
        {90, 200, 255},   // tag_alt
        {255, 0, 255},    // track
        {56, 150, 170},   // runway
        {110, 210, 230},  // runway_label
        {16, 100, 32},    // decoration (unused)
        RampMode::kColor,
        {0xFF, 0x4A, 0x2A}, {0xFF, 0xD2, 0x4A}, {0x39, 0xD0, 0xFF},
        DecorationId::kNone, false,
    },
    // 1 — Phosphor (green CRT + sweep; brightness ramp)
    {
        "Phosphor",
        {0x02, 0x0A, 0x04},
        {0x1E, 0x8A, 0x3C},
        {0x39, 0xD0, 0x6A},
        {0x39, 0xD0, 0x6A},
        {0x2E, 0xC0, 0x5A},
        {0x39, 0xD0, 0x6A},
        {0x1E, 0x8A, 0x3C},
        {0x1E, 0x8A, 0x3C},
        {0x39, 0xD0, 0x6A},
        {0x39, 0xD0, 0x6A},  // sweep wedge color
        RampMode::kBrightness,
        {0x39, 0xD0, 0x6A}, {0x39, 0xD0, 0x6A}, {0x39, 0xD0, 0x6A},
        DecorationId::kSweep, true,
    },
    // 2 — Amber CRT (brightness ramp)
    {
        "Amber CRT",
        {0x0C, 0x05, 0x00},
        {0x8A, 0x5A, 0x10},
        {0xFF, 0xC0, 0x40},
        {0xFF, 0xC0, 0x40},
        {0xF0, 0xB0, 0x30},
        {0xFF, 0xC0, 0x40},
        {0x8A, 0x5A, 0x10},
        {0x8A, 0x5A, 0x10},
        {0xFF, 0xC0, 0x40},
        {0xFF, 0xC0, 0x40},
        RampMode::kBrightness,
        {0xFF, 0xC0, 0x40}, {0xFF, 0xC0, 0x40}, {0xFF, 0xC0, 0x40},
        DecorationId::kNone, false,
    },
    // 3 — Vice (neon pink/violet; ramp pink->amber->cyan)
    {
        "Vice",
        {0x12, 0x04, 0x1F},
        {0xFF, 0x2A, 0x9D},   // rings
        {0xF0, 0xE0, 0xFF},
        {0xFF, 0xFF, 0xFF},
        {0x2A, 0xF5, 0xFF},
        {0xFF, 0xD2, 0x4A},
        {0x7A, 0x2A, 0xFF},   // crosshairs
        {0xFF, 0x2A, 0x9D},
        {0xFF, 0x8A, 0xD0},
        {0x7A, 0x2A, 0xFF},
        RampMode::kColor,
        {0xFF, 0x3A, 0x5A}, {0xFF, 0xD2, 0x4A}, {0x2A, 0xF5, 0xFF},
        DecorationId::kNone, false,
    },
    // 4 — Mission Control (navy/gold + starfield; ramp red->white->gold)
    {
        "Mission Control",
        {0x08, 0x12, 0x28},
        {0xD4, 0xA5, 0x44},   // gold rings
        {0xE8, 0xE2, 0xCE},
        {0xE8, 0xE2, 0xCE},
        {0xD4, 0xA5, 0x44},
        {0xB8, 0xC8, 0xE8},
        {0xD4, 0xA5, 0x44},
        {0x6A, 0x84, 0xB0},
        {0x9A, 0xB4, 0xD8},
        {0xE8, 0xE2, 0xCE},   // star tint
        RampMode::kColor,
        {0xC8, 0x33, 0x2A}, {0xE8, 0xE2, 0xCE}, {0xD4, 0xA5, 0x44},
        DecorationId::kStarfield, false,
    },
    // 5 — The Meatball (NASA blue/white rings + red swoosh; ramp gold->white->orange)
    {
        "The Meatball",
        {0x0B, 0x1E, 0x5B},
        {0xE8, 0xE2, 0xCE},   // white rings
        {0xFF, 0xFF, 0xFF},
        {0xFF, 0xFF, 0xFF},
        {0xE8, 0xE2, 0xCE},
        {0xB8, 0xC8, 0xE8},
        {0xC8, 0x33, 0x2A},
        {0xB8, 0xC8, 0xE8},
        {0xE8, 0xE2, 0xCE},
        {0xC8, 0x33, 0x2A},   // red swoosh
        RampMode::kColor,
        {0xFF, 0xD2, 0x4A}, {0xE8, 0xE2, 0xCE}, {0xFF, 0x8C, 0x5A},
        DecorationId::kMeatball, false,
    },
};
// clang-format on

}  // namespace ui::radar
```

- [ ] Write the failing table test. Create `test/native/test_theme_table/test_theme_table.cpp`:

```cpp
#include <unity.h>

#include <cstring>

#include "ui/theme_table.h"

using namespace ui::radar;

void test_six_themes_present() {
  TEST_ASSERT_EQUAL_size_t(6, kThemeCount);
}

void test_names_nonempty_and_ordered() {
  const char* expect[] = {"Midnight", "Phosphor",        "Amber CRT",
                          "Vice",     "Mission Control", "The Meatball"};
  for (size_t i = 0; i < kThemeCount; ++i) {
    TEST_ASSERT_NOT_NULL(kThemes[i].name);
    TEST_ASSERT_TRUE(kThemes[i].name[0] != '\0');
    TEST_ASSERT_EQUAL_STRING(expect[i], kThemes[i].name);
  }
}

void test_sweep_only_on_phosphor() {
  for (size_t i = 0; i < kThemeCount; ++i) {
    const bool expect_sweep = (i == 1);
    TEST_ASSERT_EQUAL(expect_sweep, kThemes[i].sweep_enabled);
  }
}

void test_decoration_ids_valid() {
  for (size_t i = 0; i < kThemeCount; ++i) {
    const DecorationId d = kThemes[i].decoration_id;
    TEST_ASSERT_TRUE(d == DecorationId::kNone || d == DecorationId::kSweep ||
                     d == DecorationId::kStarfield ||
                     d == DecorationId::kMeatball);
  }
}

void test_mono_themes_use_brightness_ramp() {
  TEST_ASSERT_EQUAL(RampMode::kBrightness, kThemes[1].ramp_mode);  // Phosphor
  TEST_ASSERT_EQUAL(RampMode::kBrightness, kThemes[2].ramp_mode);  // Amber
  TEST_ASSERT_EQUAL(RampMode::kColor, kThemes[0].ramp_mode);       // Midnight
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_six_themes_present);
  RUN_TEST(test_names_nonempty_and_ordered);
  RUN_TEST(test_sweep_only_on_phosphor);
  RUN_TEST(test_decoration_ids_valid);
  RUN_TEST(test_mono_themes_use_brightness_ramp);
  return UNITY_END();
}
```

The native test env must compile `theme_table_data.cpp`. Add it to the test build by updating the `[env:native]` section in `platformio.ini` to include the source. Change the `[env:native]` block to:

```ini
[env:native]
platform = native
test_framework = unity
build_src_filter =
  +<ui/theme_table_data.cpp>
build_flags =
  -std=gnu++17
  -DUNIT_TEST
  -Iinclude
lib_deps =
  bblanchon/ArduinoJson@^7.4.2
```

- [ ] Run — expect PASS (the table is the implementation):

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio test -e native -f test_theme_table
```

Expected:

```
5 Tests 0 Failures 0 Ignored
OK
```

- [ ] Create the theme-manager runtime (NVS + active index). Create `include/ui/theme_manager.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/theme_table.h"

namespace ui::radar {

/** Load active theme index from the "planeradar" NVS namespace. Call at boot. */
void themeInit();
/** Advance to the next theme (wraps) and persist. */
void themeNext();
/** Set a specific theme index (clamped) and persist. Used by the web API. */
void themeSet(uint8_t index);
uint8_t themeIndex();
const Theme& themeCurrent();

}  // namespace ui::radar
```

- [ ] Create `src/ui/theme_manager.cpp`:

```cpp
#include "ui/theme_manager.h"

#include <Preferences.h>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsThemeKey[] = "themeIdx";
constexpr uint8_t kDefaultThemeIndex = 0;  // Midnight

Preferences s_prefs;
uint8_t s_theme_index = kDefaultThemeIndex;

void saveThemeIndex() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsThemeKey, s_theme_index);
  s_prefs.end();
}

}  // namespace

void themeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsThemeKey, kDefaultThemeIndex);
  s_theme_index = (saved < kThemeCount) ? saved : kDefaultThemeIndex;
  s_prefs.end();
}

void themeNext() {
  s_theme_index = static_cast<uint8_t>((s_theme_index + 1) % kThemeCount);
  saveThemeIndex();
}

void themeSet(uint8_t index) {
  s_theme_index = (index < kThemeCount) ? index : kDefaultThemeIndex;
  saveThemeIndex();
}

uint8_t themeIndex() { return s_theme_index; }

const Theme& themeCurrent() { return kThemes[s_theme_index]; }

}  // namespace ui::radar
```

- [ ] Add `themeInit()` to boot. In `src/main.cpp`, add the include after line 14 (`#include "ui/radar_range.h"`):

```cpp
#include "ui/theme_manager.h"
```

and in `setup()`, immediately after `ui::radar::rangeInit();` (line 77), add:

```cpp
  ui::radar::themeInit();
```

- [ ] Build the firmware (theme code compiles on-target; renderer still uses old palette until Task 5):

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini
```

Expected: `SUCCESS`.

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/ui/theme_table.h include/ui/theme_manager.h src/ui/theme_table_data.cpp src/ui/theme_manager.cpp src/main.cpp platformio.ini test/native/test_theme_table && git commit -m "Add six-theme table + NVS-backed theme manager with native tests

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 4 — Medium-hold BOOT gesture (theme cycle) — hardware-verified

Adds a release-fired medium hold between 1 s and 3 s that cycles themes, composing safely with the existing 3 s reset (which fires on hold, not release) and the existing short tap (range).

**Files:**
- Modify: `include/config.h` (add timing constant after line 30)
- Modify: `include/services/wifi_setup.h` (add declaration after line 16)
- Modify: `src/services/wifi_setup.cpp` (ISR lines 28–43; consume function near lines 371–379)
- Modify: `src/main.cpp` (`handleBootButton`, lines 45–50)

- [ ] Add the medium-hold threshold. In `include/config.h`, after line 30 (`constexpr unsigned long kBootTapMinMs = 40UL;`), add:

```cpp
/** BOOT medium hold: >= this and < kBootResetHoldMs, fired on release = theme cycle. */
constexpr unsigned long kBootThemeHoldMs = 1000UL;
```

- [ ] Extend the ISR to latch a medium-hold on release. In `src/services/wifi_setup.cpp`, add a new volatile flag after line 22 (`volatile bool s_boot_tap_pending = false;`):

```cpp
volatile bool s_boot_theme_hold_pending = false;
```

Then replace the release branch inside `onBootButtonIsr` (lines 35–41, the `} else if (s_boot_is_down) { ... }` block) with:

```cpp
  } else if (s_boot_is_down) {
    const unsigned long held = now - s_boot_down_ms;
    if (held >= config::kBootTapMinMs && held < config::kBootThemeHoldMs) {
      s_boot_tap_pending = true;
    } else if (held >= config::kBootThemeHoldMs &&
               held < config::kBootResetHoldMs) {
      s_boot_theme_hold_pending = true;
    }
    s_boot_is_down = false;
  }
```

Note: the existing 3 s reset in `bootButtonPollLongPress` fires *while held* (before release), so a hold that reaches 3 s triggers reset there and never reaches this release branch — the two gestures do not collide.

- [ ] Add the consume function. In `src/services/wifi_setup.cpp`, immediately after `bootButtonConsumeTap()` (ends at line 379), add:

```cpp
bool bootButtonConsumeThemeHold() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool hold = s_boot_theme_hold_pending;
  if (hold) {
    s_boot_theme_hold_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return hold;
}
```

- [ ] Declare it. In `include/services/wifi_setup.h`, after line 16 (`bool bootButtonConsumeTap();`), add:

```cpp
/** Latched medium hold (~1 s, fired on release): request theme cycle. */
bool bootButtonConsumeThemeHold();
```

- [ ] Dispatch the gesture. In `src/main.cpp`, add the include for the theme manager (already added in Task 3) and replace `handleBootButton` (lines 45–50) with:

```cpp
void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeThemeHold()) {
    ui::radar::themeNext();
    Serial.printf("Theme: %s\n", ui::radar::themeCurrent().name);
    if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
      ui::radarDisplayDraw();
    }
  }
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}
```

- [ ] Build and flash:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini -t upload
```

Expected: `SUCCESS` then `Writing at 0x... (100 %)` and `Hard resetting via RTS pin...`.

- [ ] MANUAL VERIFY on device (serial monitor open: `pio device monitor -e supermini`):
  - Short tap BOOT (< 1 s): serial prints `Range: ...`; ring scale label changes. (Range unchanged from before.)
  - Hold BOOT ~1.5 s then release: serial prints `Theme: Phosphor` (then `Amber CRT`, etc. on repeat). Screen colors do not change yet only if Task 5 is not done; if Task 5 is done, the whole radar recolors. At this stage expect only the serial line.
  - Hold BOOT ≥ 3 s: serial prints `BOOT held — resetting WiFi` and the device reboots into the setup portal. (Reset unchanged.)

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/config.h include/services/wifi_setup.h src/services/wifi_setup.cpp src/main.cpp && git commit -m "Add ~1s medium-hold BOOT gesture to cycle themes (release-fired)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 5 — Theme-driven palette + altitude coloring — hardware-verified

Rewrites `initPalette()` to copy from the active theme and colors aircraft icons by altitude. Tags keep theme tag colors. This is the visible payoff of Tasks 2–4.

**Files:**
- Modify: `src/ui/radar_display.cpp` (`initPalette` lines 177–200; add ramp helper; `drawAircraft` loop lines 531–539; includes near line 15)

- [ ] Add includes. In `src/ui/radar_display.cpp`, after line 15 (`#include "ui/radar_theme.h"`), add:

```cpp
#include "ui/altitude_ramp.h"
#include "ui/theme_manager.h"
```

- [ ] Add a BGR-aware color helper and rewrite `initPalette`. In `src/ui/radar_display.cpp`, replace the whole `initPalette()` function (lines 177–200) with:

```cpp
/** color565 with the GC9A01 BGR swap applied when configured. */
uint16_t themeColor(const radar::Rgb8& c) {
  if (config::kDisplayRgbOrder) {
    return tft.color565(c.b, c.g, c.r);
  }
  return tft.color565(c.r, c.g, c.b);
}

void initPalette() {
  const radar::Theme& t = radar::themeCurrent();
  radar::kColorBackground = themeColor(t.bg);
  radar::kColorGrid = themeColor(t.grid);
  radar::kColorLabel = themeColor(t.label);
  radar::kColorCenter = themeColor(t.center);
  radar::kColorAircraft = themeColor(t.ramp_low);  // default; per-plane overridden
  radar::kColorTrackVector = themeColor(t.track);
  radar::kColorTagType = themeColor(t.tag_type);
  radar::kColorTagAltitude = themeColor(t.tag_alt);
  radar::kColorRunway = themeColor(t.runway);
  radar::kColorRunwayLabel = themeColor(t.runway_label);
}

/** Per-aircraft icon color from altitude, honoring the theme's ramp mode. */
uint16_t aircraftColorForAltitude(int32_t alt_ft) {
  const radar::Theme& t = radar::themeCurrent();
  radar::Rgb8 c;
  if (t.ramp_mode == radar::RampMode::kBrightness) {
    c = radar::rampBrightness(alt_ft, t.ramp_low);
  } else {
    c = radar::rampColor(alt_ft, t.ramp_low, t.ramp_mid, t.ramp_high);
  }
  return themeColor(c);
}
```

- [ ] Apply altitude color in the aircraft draw loop. In `src/ui/radar_display.cpp`, replace the in-ring draw loop (lines 531–539, the `sortDrawItemsFarFirst(...)` block through the closing brace of that `for`) with:

```cpp
  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    const uint16_t ac_color = aircraftColorForAltitude(planes[i].alt_ft);
    drawSpeedVector(x, y, planes[i].nose_deg, planes[i].track_deg,
                    planes[i].gs_knots, radar::kColorTrackVector);
    drawHeadingTriangle(x, y, planes[i].nose_deg, ac_color);
  }
```

Also apply the ramp to the beyond-ring rim dots. Replace `drawBeyondRingDot` (lines 270–273) with:

```cpp
void drawBeyondRingDot(int x, int y, uint16_t color) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx, color);
}
```

and its two call sites: in `drawAircraft`, the beyond-ring dots are built without altitude in the current struct, so extend `BeyondDotDrawItem` (lines 455–459) to carry a color:

```cpp
struct BeyondDotDrawItem {
  int x = 0;
  int y = 0;
  int dist_sq = 0;
  uint16_t color = 0;
};
```

In the collect loop (lines 514–523), set the color when pushing a dot — replace that block with:

```cpp
    int dot_x = 0;
    int dot_y = 0;
    if (!beyondRingEdgeDotFromLatLon(planes[i].lat, planes[i].lon, &dot_x,
                                     &dot_y)) {
      continue;
    }
    dots[dot_count].x = dot_x;
    dots[dot_count].y = dot_y;
    dots[dot_count].dist_sq = distSqFromCenter(dot_x, dot_y);
    dots[dot_count].color = aircraftColorForAltitude(planes[i].alt_ft);
    ++dot_count;
```

and the draw loop (lines 526–529) to:

```cpp
  sortBeyondDotsFarFirst(dots, dot_count);
  for (size_t d = 0; d < dot_count; ++d) {
    drawBeyondRingDot(dots[d].x, dots[d].y, dots[d].color);
  }
```

- [ ] Build and flash:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini -t upload
```

Expected: `SUCCESS` then upload completes with `Hard resetting`.

- [ ] MANUAL VERIFY on device:
  - Radar renders with the Midnight palette (matches the prior stock look).
  - Aircraft icons are warm-red at low altitude, amber mid, cyan high (against known traffic). Beyond-ring rim dots share the same coloring.
  - Medium-hold BOOT cycles: Phosphor (green, dimmer icons low / brighter high), Amber CRT (amber brightness ramp), Vice (pink rings, violet crosshairs), Mission Control (navy bg, gold rings), The Meatball (blue bg, white rings). Each redraws immediately.
  - Power-cycle: the last selected theme persists (NVS).

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add src/ui/radar_display.cpp && git commit -m "Drive palette from active theme; color aircraft by altitude ramp

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 6 — Trails ring buffer + rendering — hardware-verified

Fixed 64×8 lat/lon ring buffer keyed by hex, drawn as fading dots behind the icons. ~4 KB static.

**Files:**
- Create: `include/ui/trails.h`
- Create: `src/ui/trails.cpp`
- Modify: `src/ui/radar_display.cpp` (call `trailsUpdate` on refresh; draw trails before aircraft)
- Modify: `src/main.cpp` (no change needed; refresh path already central)

- [ ] Create `include/ui/trails.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "services/adsb_client.h"

namespace ui::radar {

constexpr size_t kTrailSlots = 64;
constexpr size_t kTrailPoints = 8;

struct TrailPoint {
  float lat;
  float lon;
};

/**
 * Update trails from the current aircraft list: append the head position for
 * each live hex, recycle slots for aircraft no longer present. Call once per
 * successful fetch (before drawing).
 */
void trailsUpdate(const services::adsb::Aircraft* list, size_t count);

/** Number of stored points for a hex (0 if unknown), and point accessor. */
size_t trailPointCount(const char* hex);
/**
 * Fetch the i-th trail point for hex (0 = oldest, count-1 = newest).
 * Returns false if out of range.
 */
bool trailPointAt(const char* hex, size_t i, TrailPoint* out);

}  // namespace ui::radar
```

- [ ] Create `src/ui/trails.cpp`:

```cpp
#include "ui/trails.h"

#include <cstring>

namespace ui::radar {

namespace {

struct TrailSlot {
  char hex[7];
  TrailPoint points[kTrailPoints];
  uint8_t count;   // valid points, 0..kTrailPoints
  uint8_t head;    // index of next write (ring)
  bool seen_now;   // set during the current update pass
  uint32_t age;    // updates since last seen (for recycling)
};

TrailSlot s_slots[kTrailSlots];

TrailSlot* findSlot(const char* hex) {
  for (size_t i = 0; i < kTrailSlots; ++i) {
    if (s_slots[i].count > 0 && strncmp(s_slots[i].hex, hex, 7) == 0) {
      return &s_slots[i];
    }
  }
  return nullptr;
}

TrailSlot* claimSlot(const char* hex) {
  // Prefer a truly empty slot; else recycle the oldest.
  TrailSlot* oldest = &s_slots[0];
  for (size_t i = 0; i < kTrailSlots; ++i) {
    if (s_slots[i].count == 0) {
      TrailSlot* s = &s_slots[i];
      strncpy(s->hex, hex, sizeof(s->hex) - 1);
      s->hex[sizeof(s->hex) - 1] = '\0';
      s->count = 0;
      s->head = 0;
      s->age = 0;
      return s;
    }
    if (s_slots[i].age > oldest->age) {
      oldest = &s_slots[i];
    }
  }
  strncpy(oldest->hex, hex, sizeof(oldest->hex) - 1);
  oldest->hex[sizeof(oldest->hex) - 1] = '\0';
  oldest->count = 0;
  oldest->head = 0;
  oldest->age = 0;
  return oldest;
}

void appendPoint(TrailSlot* s, float lat, float lon) {
  s->points[s->head].lat = lat;
  s->points[s->head].lon = lon;
  s->head = static_cast<uint8_t>((s->head + 1) % kTrailPoints);
  if (s->count < kTrailPoints) {
    ++s->count;
  }
}

}  // namespace

void trailsUpdate(const services::adsb::Aircraft* list, size_t count) {
  for (size_t i = 0; i < kTrailSlots; ++i) {
    s_slots[i].seen_now = false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (list[i].hex[0] == '\0') {
      continue;
    }
    TrailSlot* s = findSlot(list[i].hex);
    if (s == nullptr) {
      s = claimSlot(list[i].hex);
    }
    appendPoint(s, list[i].lat, list[i].lon);
    s->seen_now = true;
    s->age = 0;
  }
  for (size_t i = 0; i < kTrailSlots; ++i) {
    if (!s_slots[i].seen_now && s_slots[i].count > 0) {
      ++s_slots[i].age;
      if (s_slots[i].age > kTrailPoints) {
        s_slots[i].count = 0;  // fully aged out; free the slot
      }
    }
  }
}

size_t trailPointCount(const char* hex) {
  TrailSlot* s = findSlot(hex);
  return s ? s->count : 0;
}

bool trailPointAt(const char* hex, size_t i, TrailPoint* out) {
  TrailSlot* s = findSlot(hex);
  if (s == nullptr || i >= s->count) {
    return false;
  }
  // Oldest-first: head points at next write; oldest is head - count.
  const size_t start =
      (s->head + kTrailPoints - s->count) % kTrailPoints;
  const size_t idx = (start + i) % kTrailPoints;
  *out = s->points[idx];
  return true;
}

}  // namespace ui::radar
```

- [ ] Draw trails in the renderer. In `src/ui/radar_display.cpp`, add the include after the theme_manager include:

```cpp
#include "ui/trails.h"
```

Add a trail-draw helper just above `drawAircraft` (before line 485):

```cpp
void drawTrails() {
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  for (size_t i = 0; i < n; ++i) {
    if (planes[i].hex[0] == '\0') {
      continue;
    }
    const size_t pts = radar::trailPointCount(planes[i].hex);
    if (pts < 2) {
      continue;
    }
    const uint16_t base = aircraftColorForAltitude(planes[i].alt_ft);
    for (size_t p = 0; p + 1 < pts; ++p) {  // skip newest (icon sits there)
      radar::TrailPoint tp;
      if (!radar::trailPointAt(planes[i].hex, p, &tp)) {
        continue;
      }
      float dx_km = 0.0f, dy_km = 0.0f, dist_km = 0.0f;
      offsetKmFromCenter(tp.lat, tp.lon, &dx_km, &dy_km, &dist_km);
      if (!isInsideOuterRingKm(dist_km)) {
        continue;
      }
      int x = 0, y = 0;
      latLonToScreen(tp.lat, tp.lon, &x, &y);
      // Fade: newer points brighter/larger. p=0 is oldest, so frac grows to 1.
      const float frac = static_cast<float>(p + 1) / static_cast<float>(pts);
      const uint8_t alpha = static_cast<uint8_t>(60 + frac * 160);
      const uint16_t dot_color =
          s_draw->alphaBlend(alpha, base, radar::kColorBackground);
      s_draw->fillSmoothCircle(x, y, frac > 0.6f ? 2 : 1, dot_color);
    }
  }
}
```

Then call it inside `drawAircraft`, as the first drawing action, before the beyond-ring dots. Insert at the very top of `drawAircraft` after `initLabelMetrics();` (line 486):

```cpp
  drawTrails();
```

- [ ] Update trails on each successful fetch. In `src/ui/radar_display.cpp`, in `radarDisplayRefreshAircraft()` (lines 703–712), add a trails update right after `initPalette();`:

```cpp
  radar::trailsUpdate(services::adsb::aircraftList(),
                      services::adsb::aircraftCount());
```

- [ ] Build and flash:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini -t upload
```

Expected: `SUCCESS`; RAM summary rises ~4 KB (`64 * (7 + 8*8 + ...) ≈ 4.9 KB`).

- [ ] MANUAL VERIFY on device: moving aircraft leave a short tail of 1–2 px dots fading toward the background behind each icon; tails stay geographically correct when you change range (medium data is coordinate-based, not pixel-based). Static/first-seen aircraft show no tail until they move.

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/ui/trails.h src/ui/trails.cpp src/ui/radar_display.cpp && git commit -m "Add coordinate-based fading trails (64x8 ring buffer)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 7 — Decorations (sweep / starfield / meatball swoosh) + emergency halo — hardware-verified

Per-theme decorations dispatched by the active `DecorationId`, plus a flashing halo on emergency-squawk aircraft.

**Files:**
- Create: `include/ui/decorations.h`
- Create: `src/ui/decorations.cpp`
- Modify: `src/ui/radar_display.cpp` (call decorations in `drawStaticGrid`; draw emergency halo in aircraft loop)

- [ ] Create `include/ui/decorations.h`:

```cpp
#pragma once

#include <LovyanGFX.hpp>

namespace ui::radar {

/** Draw the active theme's background decoration (starfield / meatball swoosh). */
void drawThemeDecoration(lgfx::LGFXBase& gfx);

/** Draw the rotating sweep wedge for themes with sweep enabled. Advances phase. */
void drawSweep(lgfx::LGFXBase& gfx);

}  // namespace ui::radar
```

- [ ] Create `src/ui/decorations.cpp`:

```cpp
#include "ui/decorations.h"

#include <cmath>

#include "config.h"
#include "ui/radar_theme.h"
#include "ui/theme_manager.h"

namespace ui::radar {

namespace {

uint16_t decoColor565(const Rgb8& c) {
  if (config::kDisplayRgbOrder) {
    return lgfx::color565(c.b, c.g, c.r);
  }
  return lgfx::color565(c.r, c.g, c.b);
}

float s_sweep_deg = 0.0f;

void drawStarfield(lgfx::LGFXBase& gfx, const Theme& t) {
  // Deterministic pseudo-random star field (fixed seed => stable across frames).
  const uint16_t star = decoColor565(t.decoration);
  uint32_t seed = 0x1234567u;
  for (int i = 0; i < 60; ++i) {
    seed = seed * 1664525u + 1013904223u;
    const int x = static_cast<int>(seed % kSize);
    seed = seed * 1664525u + 1013904223u;
    const int y = static_cast<int>(seed % kSize);
    const int dx = x - kCenterX;
    const int dy = y - kCenterY;
    if (dx * dx + dy * dy > kGridOuterRadius * kGridOuterRadius) {
      continue;
    }
    gfx.drawPixel(x, y, star);
  }
}

void drawMeatballSwoosh(lgfx::LGFXBase& gfx, const Theme& t) {
  // A red arc sweeping through the disc, evoking the NASA "meatball" vector.
  const uint16_t red = decoColor565(t.decoration);
  constexpr float kDegToRad = 0.01745329252f;
  const int r = kGridOuterRadius - 6;
  int prev_x = 0, prev_y = 0;
  bool have_prev = false;
  for (int a = -55; a <= 55; ++a) {
    const float rad = static_cast<float>(a) * kDegToRad;
    const int x = kCenterX + static_cast<int>(std::lround(std::sin(rad) * r));
    const int y = kCenterY - 24 + static_cast<int>(std::lround(-std::cos(rad) * r * 0.35f));
    if (have_prev) {
      gfx.drawWideLine(prev_x, prev_y, x, y, 1.5f, red);
    }
    prev_x = x;
    prev_y = y;
    have_prev = true;
  }
}

}  // namespace

void drawThemeDecoration(lgfx::LGFXBase& gfx) {
  const Theme& t = themeCurrent();
  switch (t.decoration_id) {
    case DecorationId::kStarfield:
      drawStarfield(gfx, t);
      break;
    case DecorationId::kMeatball:
      drawMeatballSwoosh(gfx, t);
      break;
    default:
      break;
  }
}

void drawSweep(lgfx::LGFXBase& gfx) {
  const Theme& t = themeCurrent();
  if (!t.sweep_enabled) {
    return;
  }
  constexpr float kDegToRad = 0.01745329252f;
  const uint16_t color = decoColor565(t.decoration);
  const int r = kGridOuterRadius;
  // Wedge: a few faded leading lines behind the head.
  for (int k = 0; k < 12; ++k) {
    const float a = (s_sweep_deg - static_cast<float>(k) * 2.0f) * kDegToRad;
    const int ex = kCenterX + static_cast<int>(std::lround(std::sin(a) * r));
    const int ey = kCenterY - static_cast<int>(std::lround(std::cos(a) * r));
    const uint8_t alpha = static_cast<uint8_t>(200 - k * 15);
    gfx.drawLine(kCenterX, kCenterY, ex, ey,
                 gfx.alphaBlend(alpha, color, gfx.readPixel(kCenterX, kCenterY)));
  }
  s_sweep_deg += 6.0f;
  if (s_sweep_deg >= 360.0f) {
    s_sweep_deg -= 360.0f;
  }
}

}  // namespace ui::radar
```

- [ ] Wire decorations into the grid. In `src/ui/radar_display.cpp`, add the include after the trails include:

```cpp
#include "ui/decorations.h"
```

In `drawStaticGrid` (lines 639–657), add the background decoration right after `drawRings(...)` and the sweep after the crosshairs. Replace the body between `drawRings(cx, cy, grid_r);` and `drawCenterDot(cx, cy);` (lines 649–653) with:

```cpp
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  radar::drawThemeDecoration(gfx);
  radar::drawSweep(gfx);
  initPalette();
  runway::drawLargeAirportRunways(gfx);
```

- [ ] Draw the emergency halo. In `src/ui/radar_display.cpp`, add a helper above `drawAircraft`:

```cpp
void drawEmergencyHalo(int x, int y) {
  // Flash on ~500 ms cadence using millis(); red ring around the icon.
  if ((millis() / 500) % 2 == 0) {
    return;
  }
  const uint16_t red = themeColor(radar::Rgb8{0xFF, 0x2A, 0x2A});
  s_draw->drawCircle(x, y, 12, red);
  s_draw->drawCircle(x, y, 13, red);
}
```

Then in the in-ring aircraft draw loop (the block edited in Task 5), add the halo before drawing the triangle. The loop body becomes:

```cpp
  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    const uint16_t ac_color = aircraftColorForAltitude(planes[i].alt_ft);
    if (planes[i].emergency) {
      drawEmergencyHalo(x, y);
    }
    drawSpeedVector(x, y, planes[i].nose_deg, planes[i].track_deg,
                    planes[i].gs_knots, radar::kColorTrackVector);
    drawHeadingTriangle(x, y, planes[i].nose_deg, ac_color);
  }
```

Note: because the frame is composited once per fetch (every 3 s), the halo flash is one state per fetch, not a smooth blink; that is acceptable for a rare emergency indicator. (If continuous blink is wanted later, drive a redraw timer — out of scope here.)

- [ ] Build and flash:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini -t upload
```

Expected: `SUCCESS`.

- [ ] MANUAL VERIFY on device:
  - Phosphor theme shows a rotating green sweep wedge from center; the other five themes show no sweep.
  - Mission Control shows faint gold/cream stars scattered inside the disc.
  - The Meatball shows a red swoosh arc across the disc.
  - Emergency halo: hard to force without a real 7700 squawk. Sanity-check by temporarily hardcoding `planes[0].emergency = true` in a throwaway build, confirming the red ring appears around one icon, then revert. (Do not commit the throwaway.)

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/ui/decorations.h src/ui/decorations.cpp src/ui/radar_display.cpp && git commit -m "Add per-theme decorations (sweep/starfield/swoosh) + emergency halo

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 8 — Selection auto-clear logic (pure, TDD) + on-device state

Selection is ephemeral: cleared on phone deselect, plane loss, or ~30 s with no phone polls. The timeout predicate is pure and `native`-tested; the state + draw are on device.

**Files:**
- Create: `include/ui/selection_logic.h`
- Create: `test/native/test_selection_logic/test_selection_logic.cpp`
- Create: `include/ui/selection.h`
- Create: `src/ui/selection.cpp`
- Modify: `src/main.cpp` (`selectionTick()` each loop)

- [ ] Add the config constant. In `include/config.h`, after the `kBootThemeHoldMs` line added in Task 4, add:

```cpp
/** Selection auto-clears this long after the last phone poll of /api/aircraft. */
constexpr unsigned long kSelectionTimeoutMs = 30000UL;
```

- [ ] Write the failing selection-logic test. Create `test/native/test_selection_logic/test_selection_logic.cpp`:

```cpp
#include <unity.h>

#include "ui/selection_logic.h"

using ui::radar::selectionShouldClear;

void test_no_selection_never_clears() {
  TEST_ASSERT_FALSE(selectionShouldClear(false, /*present=*/false,
                                         /*last_poll_ms=*/0, /*now_ms=*/999999,
                                         /*timeout_ms=*/30000));
}

void test_clears_when_plane_absent() {
  TEST_ASSERT_TRUE(selectionShouldClear(true, /*present=*/false,
                                        /*last_poll_ms=*/1000, /*now_ms=*/1500,
                                        /*timeout_ms=*/30000));
}

void test_clears_after_poll_timeout() {
  TEST_ASSERT_TRUE(selectionShouldClear(true, /*present=*/true,
                                        /*last_poll_ms=*/1000,
                                        /*now_ms=*/1000 + 30001,
                                        /*timeout_ms=*/30000));
}

void test_holds_when_present_and_recent_poll() {
  TEST_ASSERT_FALSE(selectionShouldClear(true, /*present=*/true,
                                         /*last_poll_ms=*/1000,
                                         /*now_ms=*/1000 + 5000,
                                         /*timeout_ms=*/30000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_selection_never_clears);
  RUN_TEST(test_clears_when_plane_absent);
  RUN_TEST(test_clears_after_poll_timeout);
  RUN_TEST(test_holds_when_present_and_recent_poll);
  return UNITY_END();
}
```

- [ ] Run — expect FAIL (header missing):

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio test -e native -f test_selection_logic
```

Expected: `ui/selection_logic.h: No such file or directory`.

- [ ] Implement the predicate. Create `include/ui/selection_logic.h`:

```cpp
#pragma once

namespace ui::radar {

/**
 * Pure predicate: should the current selection be cleared?
 * - has_selection false  -> never clears.
 * - selected plane not present in the current list -> clear.
 * - now - last_poll_ms >= timeout_ms (no phone activity) -> clear.
 * Uses unsigned wrap-safe subtraction on the millis-style timestamps.
 */
inline bool selectionShouldClear(bool has_selection, bool present,
                                 unsigned long last_poll_ms,
                                 unsigned long now_ms,
                                 unsigned long timeout_ms) {
  if (!has_selection) {
    return false;
  }
  if (!present) {
    return true;
  }
  return (now_ms - last_poll_ms) >= timeout_ms;
}

}  // namespace ui::radar
```

- [ ] Run — expect PASS:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio test -e native -f test_selection_logic
```

Expected:

```
4 Tests 0 Failures 0 Ignored
OK
```

- [ ] Create the on-device selection state + draw. Create `include/ui/selection.h`:

```cpp
#pragma once

#include <LovyanGFX.hpp>

namespace ui::radar {

/** Set the selected aircraft by hex (empty/null clears). Marks a phone poll. */
void selectionSet(const char* hex);
/** Mark that the phone polled /api/aircraft just now (resets timeout). */
void selectionNotePoll();
const char* selectionHex();  // "" if none
bool selectionActive();
/** Re-evaluate auto-clear given the current aircraft list. Call each loop. */
void selectionTick();

/** Draw highlight ring on the selected plane at screen (x,y). */
void selectionDrawHighlight(lgfx::LGFXBase& gfx, int x, int y);
/** Draw the compact bottom info card for the selected plane, if any. */
void selectionDrawCard(lgfx::LGFXBase& gfx);

}  // namespace ui::radar
```

- [ ] Create `src/ui/selection.cpp`:

```cpp
#include "ui/selection.h"

#include <Arduino.h>

#include <cstring>

#include "config.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "ui/radar_theme.h"
#include "ui/selection_logic.h"

namespace ui::radar {

namespace {

char s_hex[7] = "";
unsigned long s_last_poll_ms = 0;

const services::adsb::Aircraft* findSelected() {
  if (s_hex[0] == '\0') {
    return nullptr;
  }
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* list = services::adsb::aircraftList();
  for (size_t i = 0; i < n; ++i) {
    if (strncmp(list[i].hex, s_hex, 7) == 0) {
      return &list[i];
    }
  }
  return nullptr;
}

}  // namespace

void selectionSet(const char* hex) {
  s_last_poll_ms = millis();
  if (hex == nullptr || hex[0] == '\0') {
    s_hex[0] = '\0';
    return;
  }
  strncpy(s_hex, hex, sizeof(s_hex) - 1);
  s_hex[sizeof(s_hex) - 1] = '\0';
}

void selectionNotePoll() { s_last_poll_ms = millis(); }

const char* selectionHex() { return s_hex; }

bool selectionActive() { return s_hex[0] != '\0'; }

void selectionTick() {
  const bool present = findSelected() != nullptr;
  if (selectionShouldClear(selectionActive(), present, s_last_poll_ms, millis(),
                           config::kSelectionTimeoutMs)) {
    s_hex[0] = '\0';
  }
}

void selectionDrawHighlight(lgfx::LGFXBase& gfx, int x, int y) {
  gfx.drawCircle(x, y, 14, kColorLabel);
  gfx.drawCircle(x, y, 15, kColorLabel);
}

void selectionDrawCard(lgfx::LGFXBase& gfx) {
  const services::adsb::Aircraft* ac = findSelected();
  if (ac == nullptr) {
    return;
  }
  // Compact card near the bottom of the round screen.
  constexpr int kCardW = 150;
  constexpr int kCardH = 44;
  const int cx = kCenterX;
  const int top = kSize - kCardH - 14;
  const int left = cx - kCardW / 2;

  gfx.fillRoundRect(left, top, kCardW, kCardH, 6, kColorBackground);
  gfx.drawRoundRect(left, top, kCardW, kCardH, 6, kColorLabel);

  displayFontSetSmoothSize(gfx, 0.5f);
  gfx.setTextDatum(textdatum_t::top_left);

  char line1[24];
  snprintf(line1, sizeof(line1), "%s %s",
           ac->callsign[0] ? ac->callsign : ac->hex, ac->type);
  gfx.setTextColor(kColorLabel, kColorBackground);
  gfx.drawString(line1, left + 8, top + 5);

  char line2[28];
  snprintf(line2, sizeof(line2), "%s  %dkt", ac->alt,
           static_cast<int>(ac->gs_knots));
  gfx.setTextColor(kColorTagAltitude, kColorBackground);
  gfx.drawString(line2, left + 8, top + 24);
  gfx.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui::radar
```

- [ ] Draw selection in the renderer. In `src/ui/radar_display.cpp`, add the include after the decorations include:

```cpp
#include "ui/selection.h"
```

In the in-ring aircraft loop, draw the highlight when the plane is selected. Add inside the loop (after computing `ac_color`, before the halo):

```cpp
    if (radar::selectionActive() &&
        strncmp(planes[i].hex, radar::selectionHex(), 7) == 0) {
      radar::selectionDrawHighlight(*s_draw, x, y);
    }
```

Ensure `<cstring>` is included (it is, transitively; add `#include <cstring>` near the top after `#include <cstdlib>` on line 7 to be safe).

Then draw the card once per frame. In `renderFrame()` (lines 675–683), after `drawAircraft();` inside the DrawScope block, add the card draw. Replace the DrawScope block (lines 676–680) with:

```cpp
  {
    const DrawScope scope(s_frame);
    drawAircraft();
    radar::selectionDrawCard(s_frame);
  }
```

- [ ] Tick selection each loop. In `src/main.cpp`, add the include after `#include "ui/theme_manager.h"`:

```cpp
#include "ui/selection.h"
```

In `loop()`, add a tick right after `wifiLoop();` (line 87):

```cpp
  ui::radar::selectionTick();
```

- [ ] Build and flash:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini -t upload
```

Expected: `SUCCESS`. (No selection is settable yet without the web API — Task 9 — so the screen looks unchanged; this validates it compiles and does not crash.)

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/config.h include/ui/selection_logic.h include/ui/selection.h src/ui/selection.cpp src/ui/radar_display.cpp src/main.cpp test/native/test_selection_logic && git commit -m "Add ephemeral selection state (highlight + bottom card) with native timeout test

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 9 — Web app build pipeline (gzip index.html into PROGMEM)

Adds `webapp/index.html`, the desktop fixture, and `scripts/build_webapp.py` wired as a PlatformIO pre-build script that regenerates `include/web/webapp_gz.h`.

**Files:**
- Create: `webapp/index.html`
- Create: `webapp/fixtures/aircraft.json`
- Create: `scripts/build_webapp.py`
- Modify: `platformio.ini` (`extra_scripts` line 10)

- [ ] Create the phone app. Create `webapp/index.html`:

```html
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Plane Radar</title>
<style>
  :root { --bg:#0b0f1a; --panel:#141b2e; --line:#243049; --fg:#e8eefc;
          --dim:#8a97b5; --accent:#39d0ff; --warn:#ffb23a; }
  * { box-sizing:border-box; }
  body { margin:0; font:15px/1.4 -apple-system,system-ui,sans-serif;
         background:var(--bg); color:var(--fg); }
  header { position:sticky; top:0; background:var(--panel);
           border-bottom:1px solid var(--line); padding:10px 14px;
           display:flex; justify-content:space-between; align-items:center; }
  header h1 { font-size:16px; margin:0; letter-spacing:.5px; }
  #meta { font-size:12px; color:var(--dim); }
  #stale { display:none; background:var(--warn); color:#201400;
           text-align:center; padding:6px; font-size:13px; font-weight:600; }
  ul { list-style:none; margin:0; padding:0; }
  li { border-bottom:1px solid var(--line); padding:10px 14px; }
  .row { display:flex; justify-content:space-between; align-items:baseline; }
  .cs { font-weight:700; }
  .em { color:var(--warn); }
  .sub { color:var(--dim); font-size:13px; }
  .dossier { margin-top:10px; display:none; }
  .dossier.open { display:block; }
  .dossier img { max-width:100%; border-radius:8px; display:block; margin-bottom:6px; }
  .credit { font-size:11px; color:var(--dim); }
  .grid { display:grid; grid-template-columns:auto auto; gap:2px 14px;
          font-size:13px; margin-top:6px; }
  .grid .k { color:var(--dim); }
  #drawerBtn { background:none; border:1px solid var(--line); color:var(--fg);
               border-radius:6px; padding:4px 10px; font-size:13px; }
  #drawer { display:none; padding:12px 14px; background:var(--panel);
            border-bottom:1px solid var(--line); }
  #drawer.open { display:block; }
  .swatches { display:flex; gap:8px; flex-wrap:wrap; margin:6px 0 12px; }
  .sw { width:34px; height:34px; border-radius:8px; border:2px solid transparent;
        cursor:pointer; }
  .sw.active { border-color:var(--fg); }
  label.opt { display:flex; align-items:center; gap:8px; margin:6px 0; }
</style>

<header>
  <h1>PLANE RADAR</h1>
  <div><span id="meta">—</span>
    <button id="drawerBtn" onclick="toggleDrawer()">Settings</button></div>
</header>
<div id="stale">Connection lost — showing last data</div>

<div id="drawer">
  <div class="sub">Theme</div>
  <div class="swatches" id="swatches"></div>
  <label class="opt"><span>Range</span>
    <select id="rangeSel" onchange="pushSettings()"></select></label>
  <label class="opt"><input type="checkbox" id="milesChk" onchange="pushSettings()">
    Distances in miles</label>
  <label class="opt"><input type="checkbox" id="rwyChk" onchange="pushSettings()">
    Show runways</label>
</div>

<ul id="list"></ul>

<script>
const THEMES = ["Midnight","Phosphor","Amber CRT","Vice","Mission Control","The Meatball"];
const SW_BG = ["#04101c","#020a04","#0c0500","#12041f","#081228","#0b1e5b"];
const RANGES = ["5","10","15","25"];
const routeCache = {};
let fails = 0, state = null, openHex = null;

function api(path, opts){ return fetch(path, opts); }

async function poll(){
  try {
    const r = await api("/api/aircraft");
    if(!r.ok) throw new Error(r.status);
    state = await r.json();
    fails = 0;
    document.getElementById("stale").style.display = "none";
    render();
  } catch(e) {
    fails++;
    if(fails >= 2) document.getElementById("stale").style.display = "block";
  }
}

function fmtDist(km){
  if(state && state.useMiles) return (km/1.609344).toFixed(1)+" mi";
  return km.toFixed(1)+" km";
}

function render(){
  if(!state) return;
  document.getElementById("meta").textContent =
    THEMES[state.theme] + " · " + state.aircraft.length + " ac";
  const list = document.getElementById("list");
  const planes = [...state.aircraft].sort((a,b)=>a.distance-b.distance);
  list.innerHTML = "";
  for(const p of planes){
    const li = document.createElement("li");
    const sel = p.hex === openHex;
    li.innerHTML =
      `<div class="row"><span class="cs ${p.emergency?'em':''}">`+
        `${p.callsign||p.hex}${p.emergency?' ⚠':''}</span>`+
        `<span class="sub">${fmtDist(p.distance)}</span></div>`+
      `<div class="sub">${p.type||'—'} · ${p.alt_ft>-1000000?p.alt_ft+' ft':'—'}`+
        ` · ${Math.round(p.gs)} kt · ${Math.round(p.track)}°</div>`+
      `<div class="dossier ${sel?'open':''}" id="d-${p.hex}"></div>`;
    li.onclick = ()=>select(p);
    list.appendChild(li);
    if(sel) fillDossier(p);
  }
}

async function select(p){
  if(openHex === p.hex){ openHex = null; await setSel(null); render(); return; }
  openHex = p.hex;
  await setSel(p.hex);
  render();
}
function setSel(hex){
  return api("/api/select", {method:"POST",
    headers:{"Content-Type":"application/json"},
    body:JSON.stringify({hex:hex})});
}

async function fillDossier(p){
  const d = document.getElementById("d-"+p.hex);
  if(!d) return;
  d.innerHTML = `<div class="grid">`+
    `<span class="k">Alt</span><span>${p.alt_ft>-1000000?p.alt_ft+' ft':'—'}</span>`+
    `<span class="k">GS</span><span>${Math.round(p.gs)} kt</span>`+
    `<span class="k">V/S</span><span>${p.vs_fpm||0} fpm</span>`+
    `<span class="k">Squawk</span><span>${p.squawk||'—'}</span>`+
    `<span class="k">Heading</span><span>${Math.round(p.track)}°</span>`+
    `<span class="k">Distance</span><span>${fmtDist(p.distance)}</span>`+
    `<span class="k">Route</span><span id="rt-${p.hex}">…</span></div>`;
  loadPhoto(p, d);
  loadRoute(p);
}

async function loadPhoto(p, d){
  try {
    const r = await fetch(`https://api.planespotters.net/pub/photos/hex/${p.hex}`);
    const j = await r.json();
    const ph = j.photos && j.photos[0];
    if(!ph) return;
    const img = document.createElement("img");
    img.src = ph.thumbnail_large ? ph.thumbnail_large.src : ph.thumbnail.src;
    const cr = document.createElement("div");
    cr.className = "credit";
    cr.textContent = "Photo © " + (ph.photographer || "unknown") + " / planespotters.net";
    d.prepend(cr); d.prepend(img);
  } catch(e){}
}

async function loadRoute(p){
  const cs = (p.callsign||"").trim();
  const el = document.getElementById("rt-"+p.hex);
  if(!cs){ if(el) el.textContent="—"; return; }
  if(routeCache[cs]){ if(el) el.textContent=routeCache[cs]; return; }
  try {
    const r = await fetch(`https://api.adsbdb.com/v0/callsign/${cs}`);
    const j = await r.json();
    const fr = j.response && j.response.flightroute;
    const txt = fr ? `${fr.origin.iata_code} → ${fr.destination.iata_code}` : "—";
    routeCache[cs] = txt;
    if(el) el.textContent = txt;
  } catch(e){ if(el) el.textContent = "—"; }
}

function toggleDrawer(){ document.getElementById("drawer").classList.toggle("open"); }

function buildDrawer(){
  const sw = document.getElementById("swatches");
  THEMES.forEach((name,i)=>{
    const b = document.createElement("div");
    b.className = "sw"; b.style.background = SW_BG[i]; b.title = name;
    b.onclick = ()=>{ pushSettings({theme:i}); };
    b.id = "sw-"+i; sw.appendChild(b);
  });
  const rs = document.getElementById("rangeSel");
  RANGES.forEach((km,i)=>{
    const o = document.createElement("option"); o.value=i; o.textContent=km+" km";
    rs.appendChild(o);
  });
}

function syncDrawer(){
  if(!state) return;
  document.querySelectorAll(".sw").forEach((e,i)=>
    e.classList.toggle("active", i===state.theme));
  document.getElementById("rangeSel").value = state.rangeIdx;
  document.getElementById("milesChk").checked = !!state.useMiles;
  document.getElementById("rwyChk").checked = !!state.showRunways;
}

async function pushSettings(override){
  const body = Object.assign({
    theme: state?state.theme:0,
    rangeIdx: +document.getElementById("rangeSel").value,
    useMiles: document.getElementById("milesChk").checked,
    showRunways: document.getElementById("rwyChk").checked,
  }, override||{});
  await api("/api/settings", {method:"POST",
    headers:{"Content-Type":"application/json"}, body:JSON.stringify(body)});
  await poll(); syncDrawer();
}

buildDrawer();
poll().then(syncDrawer);
setInterval(()=>{ poll().then(syncDrawer); }, 3000);
</script>
```

- [ ] Create the desktop fixture. Create `webapp/fixtures/aircraft.json`:

```json
{
  "lat": 52.3676,
  "lon": 4.9041,
  "rangeIdx": 1,
  "theme": 0,
  "useMiles": false,
  "showRunways": true,
  "selected": null,
  "aircraft": [
    {"hex":"484123","callsign":"KLM1234","type":"B738","alt_ft":8200,
     "gs":312,"vs_fpm":1400,"squawk":"1000","emergency":false,
     "lat":52.40,"lon":4.95,"track":95,"distance":4.2},
    {"hex":"3c4567","callsign":"DLH88X","type":"A320","alt_ft":33000,
     "gs":420,"vs_fpm":0,"squawk":"7700","emergency":true,
     "lat":52.30,"lon":4.80,"track":270,"distance":11.8}
  ]
}
```

- [ ] Create the build script. Create `scripts/build_webapp.py`:

```python
# PlatformIO pre-build: gzip webapp/index.html -> include/web/webapp_gz.h
# Wired via extra_scripts = pre:scripts/build_webapp.py

Import("env")

import gzip
from pathlib import Path

ROOT = Path(env.subst("$PROJECT_DIR"))
SRC = ROOT / "webapp" / "index.html"
OUT = ROOT / "include" / "web" / "webapp_gz.h"


def build_webapp(*_args, **_kwargs):
    raw = SRC.read_bytes()
    gz = gzip.compress(raw, compresslevel=9)
    lines = [
        "// Generated by scripts/build_webapp.py -- do not edit.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <pgmspace.h>",
        "",
        "namespace web {",
        "",
        f"constexpr size_t kWebAppRawLen = {len(raw)};",
        f"constexpr size_t kWebAppGzLen = {len(gz)};",
        "",
        "const unsigned char kWebAppGz[] PROGMEM = {",
    ]
    for i in range(0, len(gz), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in gz[i:i + 16])
        lines.append(f"  {chunk},")
    lines += ["};", "", "}  // namespace web", ""]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"build_webapp: {len(raw)} B -> {len(gz)} B gz -> {OUT.name}")


build_webapp()
```

- [ ] Wire it into PlatformIO. In `platformio.ini`, change line 10 from:

```ini
extra_scripts = post:scripts/merge_firmware.py
```

to:

```ini
extra_scripts =
  pre:scripts/build_webapp.py
  post:scripts/merge_firmware.py
```

- [ ] Generate the header by running a build (the pre-script runs automatically):

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini
```

Expected: a `build_webapp: NNNN B -> MMMM B gz -> webapp_gz.h` line early in the output, then `SUCCESS`. Confirm the header exists:

```bash
head -8 /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar/include/web/webapp_gz.h
```

Expected: the generated banner + `namespace web {` + `constexpr size_t kWebAppGzLen = ...`.

- [ ] MANUAL VERIFY (desktop): open `webapp/index.html` in a browser with the fixture. Quick check with a tiny static server that maps `/api/aircraft` to the fixture:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar/webapp && python3 -c "import http.server,os; os.rename('fixtures/aircraft.json','aircraft.json') if not os.path.exists('aircraft.json') else None" 2>/dev/null; python3 -m http.server 8099
```

Then browse `http://localhost:8099/index.html` after temporarily changing the poll URL — simpler: just confirm the file opens and the list renders from the fixture by loading `http://localhost:8099/index.html` and, in devtools, running `fetch('fixtures/aircraft.json').then(r=>r.json()).then(j=>{state=j;render();})`. Expected: two rows (KLM1234, DLH88X with ⚠), sorted by distance; clicking a row opens a dossier. (Photo/route calls hit the live APIs from your browser.) Stop the server with Ctrl-C; remove the temp `aircraft.json` copy if created.

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add webapp/index.html webapp/fixtures/aircraft.json scripts/build_webapp.py include/web/webapp_gz.h platformio.ini && git commit -m "Add phone web app + gzip build pipeline into PROGMEM header

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 10 — JSON API + serve the app on WiFiManager's server — hardware-verified

Registers `GET /`, `GET /api/aircraft`, `POST /api/select`, `POST /api/settings`, `GET /api/status` on WiFiManager's `WebServer` via `setWebServerCallback`, without disturbing the captive portal routes.

**Files:**
- Create: `include/services/web_app.h`
- Create: `src/services/web_app.cpp`
- Modify: `src/services/wifi_setup.cpp` (register the callback in `ensureWifiManager`)

- [ ] Create `include/services/web_app.h`:

```cpp
#pragma once

#include <WebServer.h>

namespace services::web_app {

/** Register the companion-app + API routes on WiFiManager's WebServer. */
void registerRoutes(WebServer& server);

}  // namespace services::web_app
```

- [ ] Create `src/services/web_app.cpp`:

```cpp
#include "services/web_app.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>

#include "config.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/selection.h"
#include "ui/theme_manager.h"
#include "web/webapp_gz.h"

namespace services::web_app {

namespace {

constexpr float kKmPerDeg = 111.0f;

float distanceKm(float lat, float lon) {
  const float dx =
      static_cast<float>(lon - services::location::lon()) * kKmPerDeg;
  const float dy =
      static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  return sqrtf(dx * dx + dy * dy);
}

WebServer* s_server = nullptr;

void handleIndex() {
  s_server->sendHeader("Content-Encoding", "gzip");
  s_server->sendHeader("Cache-Control", "max-age=86400");
  s_server->send_P(200, "text/html",
                   reinterpret_cast<const char*>(web::kWebAppGz),
                   web::kWebAppGzLen);
}

void handleAircraft() {
  // Static document sized for 64 aircraft; ArduinoJson v7 grows into heap only
  // as needed, but we cap the array by kMaxAircraft so it stays bounded.
  JsonDocument doc;
  doc["lat"] = services::location::lat();
  doc["lon"] = services::location::lon();
  doc["rangeIdx"] = ui::radar::rangeIndex();
  doc["theme"] = ui::radar::themeIndex();
  doc["useMiles"] = ui::radar::useMiles();
  doc["showRunways"] = ui::radar::showRunways();
  const char* sel = ui::radar::selectionHex();
  if (sel[0] != '\0') {
    doc["selected"] = sel;
  } else {
    doc["selected"] = nullptr;
  }

  JsonArray arr = doc["aircraft"].to<JsonArray>();
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* list = services::adsb::aircraftList();
  for (size_t i = 0; i < n; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["hex"] = list[i].hex;
    o["callsign"] = list[i].callsign;
    o["type"] = list[i].type;
    o["alt_ft"] = list[i].alt_ft;
    o["gs"] = list[i].gs_knots;
    o["vs_fpm"] = list[i].vs_fpm;
    o["squawk"] = list[i].squawk;
    o["emergency"] = list[i].emergency;
    o["lat"] = list[i].lat;
    o["lon"] = list[i].lon;
    o["track"] = list[i].track_deg;
    o["distance"] = distanceKm(list[i].lat, list[i].lon);
  }

  ui::radar::selectionNotePoll();  // a poll keeps any selection alive

  String out;
  serializeJson(doc, out);
  s_server->send(200, "application/json", out);
}

void handleSelect() {
  JsonDocument doc;
  if (deserializeJson(doc, s_server->arg("plain"))) {
    s_server->send(400, "application/json", R"({"error":"bad json"})");
    return;
  }
  if (doc["hex"].isNull()) {
    ui::radar::selectionSet(nullptr);
  } else {
    ui::radar::selectionSet(doc["hex"].as<const char*>());
  }
  s_server->send(200, "application/json", R"({"ok":true})");
}

void handleSettings() {
  JsonDocument doc;
  if (deserializeJson(doc, s_server->arg("plain"))) {
    s_server->send(400, "application/json", R"({"error":"bad json"})");
    return;
  }
  if (doc["theme"].is<int>()) {
    ui::radar::themeSet(static_cast<uint8_t>(doc["theme"].as<int>()));
  }
  if (doc["rangeIdx"].is<int>()) {
    const int want = doc["rangeIdx"].as<int>();
    // rangeNext() is the only mutator; cycle until we land on the target.
    for (int guard = 0; guard < static_cast<int>(ui::radar::kRangePresetCount) &&
                        ui::radar::rangeIndex() != want;
         ++guard) {
      ui::radar::rangeNext();
    }
  }
  if (doc["useMiles"].is<bool>()) {
    ui::radar::saveMilesFromPortal(doc["useMiles"].as<bool>() ? "T" : "");
  }
  if (doc["showRunways"].is<bool>()) {
    ui::radar::saveRunwaysFromPortal(doc["showRunways"].as<bool>() ? "T" : "");
  }
  s_server->send(200, "application/json", R"({"ok":true})");
}

void handleStatus() {
  JsonDocument doc;
  doc["uptime_s"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["heap"] = ESP.getFreeHeap();
  doc["ip"] = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  s_server->send(200, "application/json", out);
}

}  // namespace

void registerRoutes(WebServer& server) {
  s_server = &server;
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/aircraft", HTTP_GET, handleAircraft);
  server.on("/api/select", HTTP_POST, handleSelect);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/status", HTTP_GET, handleStatus);
  Serial.println("web_app: routes registered");
}

}  // namespace services::web_app
```

Note: `ui::radar::kRangePresetCount` is declared in `radar_range.h`; the range setter cycles because the existing API only exposes `rangeNext()` (no direct setter), keeping this change additive and matching the existing NVS save path.

- [ ] Register the routes on WiFiManager's server. WiFiManager exposes its `WebServer` as the public `std::unique_ptr<WebServer> server;` and calls `setWebServerCallback` *after* the server starts and its own routes are bound — the correct place to add ours without clobbering the portal. In `src/services/wifi_setup.cpp`, add the include after line 19 (`#include "ui/status_screens.h"`):

```cpp
#include "services/web_app.h"
```

Add a callback function in the anonymous namespace, right after `onConfigPortalApStarted` (ends at line 211):

```cpp
void onWebServerStarted() {
  if (s_wm.server) {
    services::web_app::registerRoutes(*s_wm.server);
  }
}
```

Then in `ensureWifiManager()` (lines 218–229), add the registration hook before `s_wm_configured = true;`:

```cpp
  s_wm.setWebServerCallback(onWebServerStarted);
```

(Place it right after `s_wm.setAPCallback(onConfigPortalApStarted);` on line 226.)

- [ ] Build and flash:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini -t upload
```

Expected: `SUCCESS`; serial shows `web_app: routes registered` shortly after `LAN config: http://plane-radar.local ...`.

- [ ] MANUAL VERIFY on device (phone on same LAN):
  - Browse `http://plane-radar.local` (or the printed IP). The app loads (gzip served), shows the live plane list sorted by distance.
  - Tap a row: the on-device screen shows a highlight ring on that plane and the bottom info card; the dossier expands on the phone with photo (credit shown) + route.
  - Settings drawer: tap a theme swatch → device recolors immediately; change range → device ring scale changes; toggle miles/runways → device updates. All persist across power-cycle.
  - Close the phone page: after ~30 s the on-device highlight + card auto-clear.
  - Confirm the WiFi setup portal still works: the WiFiManager pages (`/`, `/wifi`, etc.) — note our `GET /` overrides the portal landing while connected, which is intended (portal config is reachable via BOOT-reset flow and `/wifisave` etc. remain bound). Verify `http://plane-radar.local/api/status` returns JSON with heap/rssi/uptime.

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/services/web_app.h src/services/web_app.cpp src/services/wifi_setup.cpp && git commit -m "Serve phone app + JSON API on WiFiManager's WebServer

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 11 — Polish: on-device staleness indicator + acceptance pass

Device shows a stale indicator after ~15 s without a successful fetch; final acceptance checklist walk-through.

**Files:**
- Modify: `include/config.h` (add staleness constant)
- Modify: `src/main.cpp` (track last successful fetch; mark stale)
- Modify: `src/ui/radar_display.cpp` (draw a small stale badge)

- [ ] Add the staleness threshold. In `include/config.h`, after `kSelectionTimeoutMs`, add:

```cpp
/** Show an on-device stale badge after this long without a successful fetch. */
constexpr unsigned long kAdsbStaleAfterMs = 15000UL;
```

- [ ] Track last-good fetch and expose staleness. In `src/ui/radar_display.cpp`, add a file-scope flag and setter. Near the other statics in the anonymous namespace (after line 57 `bool s_frame_ready = false;`), add:

```cpp
unsigned long s_last_good_fetch_ms = 0;
```

Add an exported setter in the `ui` namespace. In `include/ui/radar_display.h`, after line 9, add:

```cpp
/** Record the time of the last successful adsb fetch (for the stale badge). */
void radarDisplayNoteFetch();
```

and in `src/ui/radar_display.cpp`, add the definition inside `namespace ui { ... }` near the bottom (before the closing `}  // namespace ui`, after `radarDisplayRefreshAircraft`):

```cpp
void radarDisplayNoteFetch() {
  s_last_good_fetch_ms = millis();
}
```

`s_last_good_fetch_ms` stays in the file-level anonymous namespace (added above); `radarDisplayNoteFetch()` lives in `namespace ui` in the same translation unit, so it can write the static directly. Add the badge draw helper above `renderFrame` (before line 675):

```cpp
void drawStaleBadge() {
  if (s_last_good_fetch_ms == 0) {
    return;
  }
  if (millis() - s_last_good_fetch_ms < config::kAdsbStaleAfterMs) {
    return;
  }
  const uint16_t warn = themeColor(radar::Rgb8{0xFF, 0xB2, 0x3A});
  s_draw->fillSmoothCircle(radar::kCenterX, 12, 4, warn);
}
```

Call it in `renderFrame()` inside the DrawScope block, after the selection card:

```cpp
  {
    const DrawScope scope(s_frame);
    drawAircraft();
    radar::selectionDrawCard(s_frame);
    drawStaleBadge();
  }
```

- [ ] Note successful fetches from main. In `src/main.cpp`, in `fetchAndDrawAircraft` (lines 52–61), record success. Replace the function with:

```cpp
void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayNoteFetch();
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}
```

- [ ] Build and flash:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio run -e supermini -t upload
```

Expected: `SUCCESS`.

- [ ] MANUAL VERIFY on device: with normal traffic, no badge. Temporarily disconnect the device's internet (e.g. block adsb.fi at the router, or pull the AP) — after ~15 s a small amber dot appears at top-center; the last aircraft stay drawn (no blank screen). Restore connectivity — badge clears on the next good fetch.

- [ ] Run the full native suite to confirm nothing regressed:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && pio test -e native
```

Expected: all four test folders pass; final summary `OK`.

- [ ] Final on-desk acceptance checklist (from the spec's Testing section) — confirm each:
  - [ ] All six themes cycled via BOOT medium-hold AND via the phone swatches.
  - [ ] Altitude colors look sane against known traffic (low warm, high cool/bright).
  - [ ] Trails visible behind moving aircraft; correct across a range change.
  - [ ] Phosphor sweep runs; Mission Control starfield + Meatball swoosh render.
  - [ ] Select/deselect from phone; on-device highlight + card appear/disappear.
  - [ ] Selection auto-clears ~30 s after closing the phone page.
  - [ ] Emergency halo appears for a 7500/7600/7700 squawk (or the hardcoded sanity check from Task 7).
  - [ ] Stale badge after ~15 s of fetch failure; recovers.
  - [ ] Plug-pull persistence of theme + range + units + runways.
  - [ ] Stock plane tags (callsign/type/alt) present for every aircraft in all themes and selection states.

- [ ] Commit:

```bash
cd /Users/selmapittman/Documents/Claude/Projects/ESP32-Plane-Radar && git add include/config.h include/ui/radar_display.h src/ui/radar_display.cpp src/main.cpp && git commit -m "Add on-device staleness badge; final acceptance pass

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Spec Coverage Table (self-review)

| Spec feature (design section) | Task(s) | Notes |
| --- | --- | --- |
| Six switchable themes (persisted NVS) | 3, 5 | `kThemes[6]`, `planeradar` namespace `themeIdx`. |
| Altitude color-coding (color + brightness ramp) | 2, 5 | `rampColor`/`rampBrightness`; mono themes use brightness. |
| Trails (~8 pts, coordinate-based, fade) | 6 | 64×8 lat/lon ring buffer, ~4.9 KB static. |
| Sweep animation (per-theme flag) | 3, 7 | `sweep_enabled` true only for Phosphor. |
| Phone companion page @ plane-radar.local | 9, 10 | gzipped SPA served from `GET /`. |
| `GET /api/aircraft` | 10 | snapshot incl. distance, theme, selected. |
| `POST /api/select` (`{"hex":...}`/null) | 10 | `handleSelect`. |
| `POST /api/settings` (theme/range/miles/runways) | 10 | `handleSettings`, existing save paths. |
| `GET /api/status` (uptime/rssi/heap) | 10 | `handleStatus`. |
| Selection highlight + on-device card (ephemeral, ~30 s) | 8, 10 | `selection_logic.h` + `selection.cpp`. |
| Button gestures: tap=range / ~1 s=theme / 3 s=reset | 4 | medium-hold fires on release 1 s–<3 s. |
| Emergency halo (7500/7600/7700) | 1, 7 | squawk parsed in Task 1, drawn in Task 7. |
| Aircraft struct extension (hex/alt_ft/vs_fpm/squawk/emergency) | 1 | native-tested extraction. |
| Photo via planespotters (credit shown) | 9 | `loadPhoto`, credit line required by API terms. |
| Route via adsbdb (cached per callsign) | 9 | `loadRoute` + `routeCache`. |
| Staleness: device badge ~15 s; phone banner (2 fails) | 9, 11 | phone `#stale` div; device amber dot. |
| Desktop-browser fixture test | 9 | `webapp/fixtures/aircraft.json`. |
| RAM budget (no PSRAM; small static allocations) | 1, 6, 10 | struct +~1.2 KB, trails ~4.9 KB, JSON via bounded doc. |
| Stock per-plane tags always present | 5, 6, 7, 8 | tag draw untouched; only icon color/decorations added. |

## Technical risks / notes surfaced from the source

1. **WiFiManager route registration:** verified in v2.0.17 that `WiFiManager::server` is a public `std::unique_ptr<WebServer>` and `setWebServerCallback` fires after the server + portal routes are bound — so `s_wm.server->on(...)` in that callback is the safe, non-portal-breaking hook. Our `GET /` overrides the WiFiManager landing page *while connected* (intended: this is the companion app), but `/wifisave`, `/wifi`, `/erase`, etc. remain bound; the portal is still reachable after a BOOT reset. If a future upstream rebase changes the callback timing, re-registration would need to move to a post-`startWebPortal()` call.

2. **`WebServer.h` include:** `web_app.h` includes `<WebServer.h>`. This is the ESP32 Arduino core's server, which WiFiManager aliases as `WM_WebServer`. The types are identical on ESP32-C3; if a compile error surfaces about `WebServer` vs `WM_WebServer`, include WiFiManager's header path or accept the pointer as `decltype(*s_wm.server)&` — not expected, but flagged.

3. **RAM:** enlarged `Aircraft` adds ~18 B × 64 ≈ 1.2 KB; trails ≈ 4.9 KB static; the JSON serialization uses a stack/heap `JsonDocument` that grows to ~4–6 KB transiently per poll (bounded by 64 aircraft). Total new steady-state static ≈ 6 KB, well within headroom given the 112.5 KB sprite is the dominant allocation and there is no PSRAM. The transient JSON doc is the main heap-churn item; it is freed per request. If heap pressure appears under many aircraft, switch `handleAircraft` to a chunked `sendContent` writer — noted, not implemented.

4. **Emergency halo blink cadence:** the frame composites once per 3 s fetch, so the `millis()`-based halo shows one blink phase per fetch rather than a smooth flash. Acceptable for a rare indicator; a continuous blink would need a redraw timer (out of scope, spec calls the halo "a small freebie").

5. **adsb.fi field types:** `alt_baro` can be the string `"ground"` or a number; `squawk` arrives as a string; `baro_rate`/`geom_rate` are numeric. The extraction module handles the string/number ambiguity explicitly (tested in Task 1). `hex` may carry a leading `~` (TIS-B) which is stripped so trail/selection keying is stable.

6. **`build_src_filter` in `[env:native]`:** the native test env must NOT compile Arduino-dependent `.cpp` files. Only `theme_table_data.cpp` is pulled in (it is header-clean). The tested logic otherwise lives in headers (`altitude_ramp.h`, `aircraft_fields.h`, `selection_logic.h`), so the native env stays Arduino-free. If PlatformIO's default `test_build_src` behavior tries to compile `src/`, the explicit `build_src_filter` confines it.
