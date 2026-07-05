#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/altitude_ramp.h"

namespace ui::radar {

enum class RampMode : uint8_t { kColor, kBrightness };
enum class DecorationId : uint8_t { kNone, kSweep, kStarfield, kMeatball };
enum class ScopeStyle : uint8_t { kNone, kCic };

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
  ScopeStyle scope_style;  // kCic draws bearing ring/ticks/square grid/brackets + map
};

// A dropped/added field changes the struct size; update deliberately.
// 13 Rgb8 (39 B) + 3 enums (RampMode, DecorationId, ScopeStyle) + 1 bool = 43
// byte-sized members, no interior padding, plus the `name` pointer, rounded
// up to pointer alignment (56 on 64-bit native, 48 on 32-bit ESP32-C3).
static_assert(sizeof(Theme) ==
                  ((sizeof(const char*) + 13 * sizeof(Rgb8) + 3 + 1 +
                    alignof(Theme) - 1) /
                   alignof(Theme)) *
                      alignof(Theme),
              "Theme field set changed - update theme_table_data.cpp entries");

constexpr size_t kThemeCount = 7;

extern const Theme kThemes[kThemeCount];

}  // namespace ui::radar
