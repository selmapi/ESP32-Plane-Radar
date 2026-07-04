#include "ui/decorations.h"

#include <cmath>

#include "config.h"
#include "ui/color_blend.h"
#include "ui/radar_theme.h"
#include "ui/theme_manager.h"

namespace ui::radar {

namespace {

uint16_t decoColor565(lgfx::LGFXBase& gfx, const Rgb8& c) {
  if (config::kDisplayRgbOrder) {
    return gfx.color565(c.b, c.g, c.r);
  }
  return gfx.color565(c.r, c.g, c.b);
}

float s_sweep_deg = 0.0f;

void drawStarfield(lgfx::LGFXBase& gfx, const Theme& t) {
  // Deterministic pseudo-random star field (fixed seed => stable across frames).
  const uint16_t star = decoColor565(gfx, t.decoration);
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
  const uint16_t red = decoColor565(gfx, t.decoration);
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
  const uint16_t color = decoColor565(gfx, t.decoration);
  const uint16_t bg = decoColor565(gfx, t.bg);
  const int r = kGridOuterRadius;
  // Wedge: a few faded leading lines behind the head, blended toward the
  // theme background (no alphaBlend/readPixel available on this LovyanGFX).
  for (int k = 0; k < 12; ++k) {
    const float a = (s_sweep_deg - static_cast<float>(k) * 2.0f) * kDegToRad;
    const int ex = kCenterX + static_cast<int>(std::lround(std::sin(a) * r));
    const int ey = kCenterY - static_cast<int>(std::lround(std::cos(a) * r));
    const uint8_t alpha = static_cast<uint8_t>(200 - k * 15);
    gfx.drawLine(kCenterX, kCenterY, ex, ey, lerpRgb565(color, bg, alpha));
  }
  s_sweep_deg += 6.0f;
  if (s_sweep_deg >= 360.0f) {
    s_sweep_deg -= 360.0f;
  }
}

}  // namespace ui::radar
