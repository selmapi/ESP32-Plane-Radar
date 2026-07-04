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
