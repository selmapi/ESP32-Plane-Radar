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
