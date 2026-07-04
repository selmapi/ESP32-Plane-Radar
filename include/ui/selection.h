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
