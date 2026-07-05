#pragma once

#include <cmath>

#include "ui/geo_transform.h"

namespace ui::radar {

/**
 * Liang-Barsky-style clip of segment (x0,y0)-(x1,y1) against a circle of radius
 * r centered at (cx,cy). Returns false if the segment misses the disc entirely.
 * On true, (x0,y0)/(x1,y1) are moved onto the disc boundary where they exit.
 * Pure integer/float math (no LovyanGFX) so it is native-testable and shared
 * with the map renderer.
 */
inline bool clipSegmentToDisc(int cx, int cy, int r, float* x0, float* y0,
                              float* x1, float* y1) {
  const float dx = *x1 - *x0;
  const float dy = *y1 - *y0;
  const float fx = *x0 - static_cast<float>(cx);
  const float fy = *y0 - static_cast<float>(cy);
  const float a = dx * dx + dy * dy;
  const float b = 2.0f * (fx * dx + fy * dy);
  const float c = fx * fx + fy * fy - static_cast<float>(r) * r;

  if (a < 1e-6f) {
    // Degenerate segment: inside iff endpoint within r.
    return (fx * fx + fy * fy) <= static_cast<float>(r) * r;
  }
  float disc = b * b - 4.0f * a * c;
  if (disc < 0.0f) {
    return false;  // no intersection with the circle at all
  }
  disc = sqrtf(disc);
  float t0 = (-b - disc) / (2.0f * a);
  float t1 = (-b + disc) / (2.0f * a);
  if (t1 < 0.0f || t0 > 1.0f) {
    return false;  // intersection is off the segment
  }
  const float e0 = t0 < 0.0f ? 0.0f : t0;
  const float e1 = t1 > 1.0f ? 1.0f : t1;
  const float nx0 = *x0 + e0 * dx;
  const float ny0 = *y0 + e0 * dy;
  const float nx1 = *x0 + e1 * dx;
  const float ny1 = *y0 + e1 * dy;
  *x0 = nx0;
  *y0 = ny0;
  *x1 = nx1;
  *y1 = ny1;
  return true;
}

/** True when the baked map (center at map_lat/map_lon) still covers the
 *  configured runtime center: within max_drift_km. Pure for native tests. */
inline bool mapCoversLocation(float map_lat, float map_lon, float loc_lat,
                              float loc_lon, float max_drift_km) {
  float dx_km = 0.0f, dy_km = 0.0f;
  offsetKmDelta(loc_lat, loc_lon, map_lat, map_lon, &dx_km, &dy_km);
  return (dx_km * dx_km + dy_km * dy_km) <= (max_drift_km * max_drift_km);
}

}  // namespace ui::radar
