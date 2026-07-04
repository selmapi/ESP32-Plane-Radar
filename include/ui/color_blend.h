#pragma once

#include <cstdint>

namespace ui::radar {

/**
 * Lerp an RGB565-packed color toward another RGB565 color by alpha/255.
 * LovyanGFX 1.2.21 has no alphaBlend/readPixel helper on this vendored
 * version, so blend manually per 5/6/5 channel. alpha=255 -> `from`,
 * alpha=0 -> `to`.
 */
inline uint16_t lerpRgb565(uint16_t from, uint16_t to, uint8_t alpha) {
  const uint16_t fr = (from >> 11) & 0x1F;
  const uint16_t fg = (from >> 5) & 0x3F;
  const uint16_t fb = from & 0x1F;
  const uint16_t tr = (to >> 11) & 0x1F;
  const uint16_t tg = (to >> 5) & 0x3F;
  const uint16_t tb = to & 0x1F;

  const uint16_t r = static_cast<uint16_t>(tr + ((fr - tr) * alpha) / 255);
  const uint16_t g = static_cast<uint16_t>(tg + ((fg - tg) * alpha) / 255);
  const uint16_t b = static_cast<uint16_t>(tb + ((fb - tb) * alpha) / 255);

  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

}  // namespace ui::radar
