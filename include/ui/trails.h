#pragma once

#include <cstddef>
#include <cstdint>

#include "services/adsb_client.h"

namespace ui::radar {

constexpr size_t kTrailSlots = 64;
constexpr size_t kTrailPoints = 8;

struct TrailPoint {
  float lat;
  float lon;
};

/**
 * Update trails from the current aircraft list: append the head position for
 * each live hex, recycle slots for aircraft no longer present. Call once per
 * successful fetch (before drawing).
 */
void trailsUpdate(const services::adsb::Aircraft* list, size_t count);

/** Number of stored points for a hex (0 if unknown), and point accessor. */
size_t trailPointCount(const char* hex);
/**
 * Fetch the i-th trail point for hex (0 = oldest, count-1 = newest).
 * Returns false if out of range.
 */
bool trailPointAt(const char* hex, size_t i, TrailPoint* out);

}  // namespace ui::radar
