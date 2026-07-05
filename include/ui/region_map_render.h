#pragma once

#include <LovyanGFX.hpp>

namespace ui::radar {

/** Draw the baked region map, clipped to the outer ring. No-op unless the
 * active theme's scope_style is kCic. Call under runways/planes. */
void drawRegionMap(lgfx::LGFXBase& gfx);

}  // namespace ui::radar
