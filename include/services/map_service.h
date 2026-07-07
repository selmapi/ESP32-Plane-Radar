#pragma once

#include <cstddef>

namespace services::map {

/** Hook invoked during long HTTP I/O (e.g. wifiLoop), same idiom as
 *  services::adsb::PollFn -- keeps WiFiManager's portal servicing during a
 *  blocking fetch. Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Loads the NVS override of the service URL (if any). Call once at boot. */
void init();

/** NVS override if set, else config::kDefaultMapServiceUrl. */
const char* serviceUrl();

/** Validates (non-empty, "http://" or "https://" prefix) and persists the
 *  service URL. Returns false (URL unchanged) on invalid input. */
bool saveServiceUrl(const char* url);

enum class RebuildResult {
  kOk,
  kNoUrlConfigured,
  kNetworkError,
  kInvalidResponse,
  kWriteError,
  kBuilding,
  kBusy,
};

/** Short human-readable string for a JSON error field. */
const char* rebuildResultMessage(RebuildResult r);

/**
 * Blocking (like adsb_client's fetchUpdate) -- fetches, validates, and
 * atomically swaps in a new /map.bin for (lat, lon, radiusKm). On any
 * failure, the existing /map.bin (or baked fallback) is left untouched.
 */
RebuildResult rebuildForLocation(double lat, double lon, float radiusKm);

}  // namespace services::map
