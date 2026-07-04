#pragma once

#include <LovyanGFX.hpp>

namespace ui::radar {

/** Draw the active theme's background decoration (starfield / meatball swoosh). */
void drawThemeDecoration(lgfx::LGFXBase& gfx);

/** Draw the rotating sweep wedge for themes with sweep enabled. Advances phase. */
void drawSweep(lgfx::LGFXBase& gfx);

}  // namespace ui::radar
