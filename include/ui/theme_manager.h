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
