#pragma once

#include <LovyanGFX.hpp>

#include "config.h"
#include "ui/altitude_ramp.h"

namespace ui::radar {

/** Pack an Rgb8 for the panel, applying the GC9A01 R/B swap when configured.
 *  Single home for the swap idiom — do not re-copy this into new files. */
inline uint16_t themeColor565(lgfx::LGFXBase& gfx, const Rgb8& c) {
  if (config::kDisplayRgbOrder) {
    return gfx.color565(c.b, c.g, c.r);
  }
  return gfx.color565(c.r, c.g, c.b);
}

}  // namespace ui::radar
