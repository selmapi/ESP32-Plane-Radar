#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Record the time of the last successful adsb fetch (for the stale badge). */
void radarDisplayNoteFetch();

}  // namespace ui
