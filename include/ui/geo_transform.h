#pragma once

#include <cmath>

namespace ui::radar {

/** Kilometers per degree of latitude (and of longitude at the equator). */
constexpr float kKmPerDeg = 111.0f;

/**
 * Flat local-tangent-plane offset (km) of (lat, lon) from a center, with the
 * standard cos(latitude) correction on the east-west (longitude) axis. North
 * is +dy. Longitude deltas shrink by cos(center_lat): at 36N, 1 deg east is
 * ~89.78 km, not 111 km.
 *
 * Header-clean (only <cmath>) so the native test env compiles it and the
 * renderer, runway overlay, and phone-distance code all share one model.
 */
inline void offsetKmDelta(float lat, float lon, float center_lat,
                          float center_lon, float* dx_km, float* dy_km) {
  const float cos_lat = cosf(center_lat * 0.01745329252f);  // deg -> rad
  *dx_km = (lon - center_lon) * kKmPerDeg * cos_lat;
  *dy_km = (lat - center_lat) * kKmPerDeg;
}

}  // namespace ui::radar
