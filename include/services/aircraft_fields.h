#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "services/adsb_client.h"

namespace services::adsb {

/** Lowercase-hex copy of "hex", dropping a leading TIS-B '~'. */
inline void extractHex(Aircraft* ac, const JsonObject& plane) {
  ac->hex[0] = '\0';
  if (!plane["hex"].is<const char*>()) {
    return;
  }
  const char* s = plane["hex"].as<const char*>();
  if (s[0] == '~') {
    ++s;
  }
  size_t i = 0;
  for (; s[i] != '\0' && i < sizeof(ac->hex) - 1; ++i) {
    const char c = s[i];
    ac->hex[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  ac->hex[i] = '\0';
}

/** Numeric barometric altitude in feet; INT32_MIN for "ground" or missing. */
inline void extractAltFt(Aircraft* ac, const JsonObject& plane) {
  ac->alt_ft = INT32_MIN;
  if (plane["alt_baro"].is<const char*>()) {
    return;  // "ground"
  }
  if (plane["alt_baro"].is<int>() || plane["alt_baro"].is<float>()) {
    ac->alt_ft = plane["alt_baro"].as<int32_t>();
  } else if (plane["alt_geom"].is<int>() || plane["alt_geom"].is<float>()) {
    ac->alt_ft = plane["alt_geom"].as<int32_t>();
  }
}

/** Vertical rate: prefer baro_rate, else geom_rate; 0 if neither present. */
inline void extractVsFpm(Aircraft* ac, const JsonObject& plane) {
  ac->vs_fpm = 0;
  if (plane["baro_rate"].is<int>() || plane["baro_rate"].is<float>()) {
    ac->vs_fpm = plane["baro_rate"].as<int16_t>();
  } else if (plane["geom_rate"].is<int>() || plane["geom_rate"].is<float>()) {
    ac->vs_fpm = plane["geom_rate"].as<int16_t>();
  }
}

inline bool squawkIsEmergency(const char* sq) {
  return strcmp(sq, "7500") == 0 || strcmp(sq, "7600") == 0 ||
         strcmp(sq, "7700") == 0;
}

/** Squawk string + emergency flag (from squawk code or adsb.fi "emergency"). */
inline void extractSquawk(Aircraft* ac, const JsonObject& plane) {
  ac->squawk[0] = '\0';
  if (plane["squawk"].is<const char*>()) {
    const char* s = plane["squawk"].as<const char*>();
    strncpy(ac->squawk, s, sizeof(ac->squawk) - 1);
    ac->squawk[sizeof(ac->squawk) - 1] = '\0';
  }
  bool emergency = squawkIsEmergency(ac->squawk);
  if (plane["emergency"].is<const char*>()) {
    const char* e = plane["emergency"].as<const char*>();
    if (e[0] != '\0' && strcmp(e, "none") != 0) {
      emergency = true;
    }
  }
  ac->emergency = emergency;
}

/** Fill hex, alt_ft, vs_fpm, squawk, emergency. Existing tag fields untouched. */
inline void extractExtendedFields(Aircraft* ac, const JsonObject& plane) {
  extractHex(ac, plane);
  extractAltFt(ac, plane);
  extractVsFpm(ac, plane);
  extractSquawk(ac, plane);
}

}  // namespace services::adsb
