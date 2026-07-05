#include "ui/region_map_render.h"

#include "ui/region_map.h"

#include <LovyanGFX.hpp>

#include <cmath>

#include "config.h"
#include "services/radar_location.h"
#include "ui/geo_transform.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/region_map_geom.h"
#include "ui/theme_color.h"
#include "ui/theme_manager.h"

namespace ui::radar {

namespace {

// CIC map palette: natural cartographic colors on the black scope background.
// Deliberate exception to theme-derived coloring - the map is CIC-only by
// design (see spec); if the map ever extends to other themes, revisit.
constexpr Rgb8 kMapHighway = {0xB4, 0xB4, 0xB4};   // road gray-white
constexpr Rgb8 kMapWater = {0x3A, 0x7A, 0xD0};     // water blue
constexpr Rgb8 kMapBoundary = {0x6E, 0x6E, 0x6E};  // county-line gray (dashed)
constexpr Rgb8 kMapTown = {0xE8, 0xE2, 0xCE};      // warm white dots + labels

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

/** Layer color: natural map palette (roads/water/boundaries), CIC-only. */
uint16_t layerColor(lgfx::LGFXBase& gfx, uint8_t layer) {
  switch (static_cast<MapLayer>(layer)) {
    case MapLayer::kHighway:
      return themeColor565(gfx, kMapHighway);
    case MapLayer::kWater:
      return themeColor565(gfx, kMapWater);
    case MapLayer::kBoundary:
      return themeColor565(gfx, kMapBoundary);
    default:
      return themeColor565(gfx, kMapTown);
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
  // Prebuilt-bin safety: don't draw a demo map under someone else's sky.
  if (!mapCoversLocation(kMapCenterLat, kMapCenterLon,
                         static_cast<float>(services::location::lat()),
                         static_cast<float>(services::location::lon()),
                         config::kMapMaxCenterDriftKm)) {
    return;
  }
  for (size_t i = 0; i < kMapSpanCount; ++i) {
    const MapSpan& span = kMapSpans[i];
    const uint16_t color = layerColor(gfx, span.layer);
    const bool dashed = static_cast<MapLayer>(span.layer) == MapLayer::kBoundary;
    drawSpan(gfx, span, color, dashed);
  }
  // Town markers: a small dot + label.
  const uint16_t town = themeColor565(gfx, kMapTown);
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
