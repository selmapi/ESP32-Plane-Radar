#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];    // formatted tag string ("12300 ft" / "GND"), unchanged
  char hex[7];     // ICAO 24-bit, lowercase hex, no leading '~'
  int32_t alt_ft;  // numeric barometric altitude in feet; INT32_MIN if unknown/ground
  int16_t vs_fpm;  // vertical rate, feet per minute; 0 if unknown
  char squawk[5];  // 4-digit transponder code, e.g. "7700"
  bool emergency;  // true when squawk is 7500/7600/7700
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

}  // namespace services::adsb
