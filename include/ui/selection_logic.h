#pragma once

namespace ui::radar {

/**
 * Pure predicate: should the current selection be cleared?
 * - has_selection false  -> never clears.
 * - selected plane not present in the current list -> clear.
 * - now - last_poll_ms >= timeout_ms (no phone activity) -> clear.
 * Uses unsigned wrap-safe subtraction on the millis-style timestamps.
 */
inline bool selectionShouldClear(bool has_selection, bool present,
                                 unsigned long last_poll_ms,
                                 unsigned long now_ms,
                                 unsigned long timeout_ms) {
  if (!has_selection) {
    return false;
  }
  if (!present) {
    return true;
  }
  return (now_ms - last_poll_ms) >= timeout_ms;
}

}  // namespace ui::radar
