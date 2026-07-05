#include "ui/region_map_render.h"

#include "ui/region_map.h"

#include <LovyanGFX.hpp>

#include <cmath>

#include "config.h"
#include "services/radar_location.h"
#include "ui/color_blend.h"
#include "ui/geo_transform.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/region_map_geom.h"
#include "ui/theme_manager.h"

namespace ui::radar {

namespace {

uint16_t mapColor565(lgfx::LGFXBase& gfx, const Rgb8& c) {
  if (config::kDisplayRgbOrder) {
    return gfx.color565(c.b, c.g, c.r);
  }
  return gfx.color565(c.r, c.g, c.b);
}

/** Baked-vertex (int16 offset) -> lat/lon degrees. */
void vertToLatLon(const MapVert& v, float* lat, float* lon) {
  *lat = kMapCenterLat + static_cast<float>(v.dlat) * kMapQuantDeg;
  *lon = kMapCenterLon + static_cast<float>(v.dlon) * kMapQuantDeg;
}

/** lat/lon -> screen, mirroring radar_display latLonToScreen exactly. */
void latLonToScreen(float lat, float lon, float* out_x, float* out_y) {
  const float outer_km = rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(kGridOuterRadius) / outer_km;
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  offsetKmDelta(lat, lon, services::location::lat(), services::location::lon(),
                &dx_km, &dy_km);
  *out_x = static_cast<float>(kCenterX) + dx_km * px_per_km;
  *out_y = static_cast<float>(kCenterY) - dy_km * px_per_km;
}

/** Layer color: dim theme grid (roads/water/boundaries) or label (towns). */
uint16_t layerColor(lgfx::LGFXBase& gfx, const Theme& t, uint8_t layer) {
  // Dim toward the background so the map sits under the sweep, not over it.
  const uint16_t grid = mapColor565(gfx, t.grid);
  const uint16_t bg = mapColor565(gfx, t.bg);
  switch (static_cast<MapLayer>(layer)) {
    case MapLayer::kHighway:
      return lerpRgb565(grid, bg, 150);   // ~60% toward bg
    case MapLayer::kWater:
      return lerpRgb565(grid, bg, 190);   // dimmer
    case MapLayer::kBoundary:
      return lerpRgb565(grid, bg, 210);   // dimmest (dashed below)
    default:
      return mapColor565(gfx, t.label);
  }
}

void drawSpan(lgfx::LGFXBase& gfx, const MapSpan& span, uint16_t color,
              bool dashed) {
  float px = 0.0f;
  float py = 0.0f;
  bool have_prev = false;
  int seg = 0;
  for (uint16_t k = 0; k < span.len; ++k) {
    float lat = 0.0f;
    float lon = 0.0f;
    vertToLatLon(kMapVerts[span.start + k], &lat, &lon);
    float x = 0.0f;
    float y = 0.0f;
    latLonToScreen(lat, lon, &x, &y);
    if (have_prev) {
      float x0 = px;
      float y0 = py;
      float x1 = x;
      float y1 = y;
      if (clipSegmentToDisc(kCenterX, kCenterY, kGridOuterRadius, &x0, &y0, &x1,
                            &y1)) {
        if (!dashed || (seg % 2 == 0)) {
          gfx.drawLine(static_cast<int>(lroundf(x0)),
                       static_cast<int>(lroundf(y0)),
                       static_cast<int>(lroundf(x1)),
                       static_cast<int>(lroundf(y1)), color);
        }
        ++seg;
      }
    }
    px = x;
    py = y;
    have_prev = true;
  }
}

}  // namespace

void drawRegionMap(lgfx::LGFXBase& gfx) {
  const Theme& t = themeCurrent();
  if (t.scope_style != ScopeStyle::kCic) {
    return;
  }
  for (size_t i = 0; i < kMapSpanCount; ++i) {
    const MapSpan& span = kMapSpans[i];
    const uint16_t color = layerColor(gfx, t, span.layer);
    const bool dashed = static_cast<MapLayer>(span.layer) == MapLayer::kBoundary;
    drawSpan(gfx, span, color, dashed);
  }
  // Town markers: a small dot + label.
  const uint16_t town = mapColor565(gfx, t.label);
  for (size_t i = 0; i < kMapTownCount; ++i) {
    const MapTown& tw = kMapTowns[i];
    const float lat = kMapCenterLat + static_cast<float>(tw.dlat) * kMapQuantDeg;
    const float lon = kMapCenterLon + static_cast<float>(tw.dlon) * kMapQuantDeg;
    float x = 0.0f;
    float y = 0.0f;
    latLonToScreen(lat, lon, &x, &y);
    const int dx = static_cast<int>(lroundf(x)) - kCenterX;
    const int dy = static_cast<int>(lroundf(y)) - kCenterY;
    if (dx * dx + dy * dy > kGridOuterRadius * kGridOuterRadius) {
      continue;  // outside the ring
    }
    gfx.fillCircle(static_cast<int>(lroundf(x)), static_cast<int>(lroundf(y)), 1,
                   town);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.setTextColor(town);
    gfx.drawString(tw.label, static_cast<int>(lroundf(x)) + 2,
                   static_cast<int>(lroundf(y)) + 2);
  }
}

}  // namespace ui::radar
