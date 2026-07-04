#pragma once

#include <cstdint>

namespace ui::radar {

/**
 * Pure predicate: should the current selection be cleared?
 * - has_selection false  -> never clears.
 * - selected plane not present in the current list -> clear.
 * - now - last_poll_ms >= timeout_ms (no phone activity) -> clear.
 * Uses unsigned wrap-safe subtraction on the millis-style timestamps.
 * Parameters are uint32_t (millis() returns uint32_t on ESP32) so the
 * wraparound arithmetic behaves identically on-device and in native tests.
 */
inline bool selectionShouldClear(bool has_selection, bool present,
                                 uint32_t last_poll_ms,
                                 uint32_t now_ms,
                                 uint32_t timeout_ms) {
  if (!has_selection) {
    return false;
  }
  if (!present) {
    return true;
  }
  return (now_ms - last_poll_ms) >= timeout_ms;
}

}  // namespace ui::radar
