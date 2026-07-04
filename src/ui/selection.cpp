#include "ui/selection.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <cstring>

#include "config.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "ui/radar_theme.h"
#include "ui/selection_logic.h"

namespace ui::radar {

namespace {

namespace fonts = lgfx::v1::fonts;

char s_hex[7] = "";
unsigned long s_last_poll_ms = 0;

const services::adsb::Aircraft* findSelected() {
  if (s_hex[0] == '\0') {
    return nullptr;
  }
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* list = services::adsb::aircraftList();
  for (size_t i = 0; i < n; ++i) {
    if (strncmp(list[i].hex, s_hex, 7) == 0) {
      return &list[i];
    }
  }
  return nullptr;
}

/** Small tag-sized text style, matching the aircraft tag idiom in radar_display.cpp. */
void applySelectionCardStyle(lgfx::LGFXBase& gfx) {
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(gfx, 0.56f);
  } else {
    displayFontSetBitmap(gfx, &fonts::FreeSansBold12pt7b);
  }
}

}  // namespace

void selectionSet(const char* hex) {
  s_last_poll_ms = millis();
  if (hex == nullptr || hex[0] == '\0') {
    s_hex[0] = '\0';
    return;
  }
  strncpy(s_hex, hex, sizeof(s_hex) - 1);
  s_hex[sizeof(s_hex) - 1] = '\0';
}

void selectionNotePoll() { s_last_poll_ms = millis(); }

const char* selectionHex() { return s_hex; }

bool selectionActive() { return s_hex[0] != '\0'; }

void selectionTick() {
  const bool present = findSelected() != nullptr;
  if (selectionShouldClear(selectionActive(), present, s_last_poll_ms, millis(),
                           config::kSelectionTimeoutMs)) {
    s_hex[0] = '\0';
  }
}

void selectionDrawHighlight(lgfx::LGFXBase& gfx, int x, int y) {
  gfx.drawCircle(x, y, 14, kColorLabel);
  gfx.drawCircle(x, y, 15, kColorLabel);
}

void selectionDrawCard(lgfx::LGFXBase& gfx) {
  const services::adsb::Aircraft* ac = findSelected();
  if (ac == nullptr) {
    return;
  }
  // Compact card near the bottom of the round screen.
  constexpr int kCardW = 150;
  constexpr int kCardH = 44;
  const int cx = kCenterX;
  const int top = kSize - kCardH - 14;
  const int left = cx - kCardW / 2;

  gfx.fillRoundRect(left, top, kCardW, kCardH, 6, kColorBackground);
  gfx.drawRoundRect(left, top, kCardW, kCardH, 6, kColorLabel);

  applySelectionCardStyle(gfx);
  gfx.setTextDatum(textdatum_t::top_left);

  char line1[24];
  snprintf(line1, sizeof(line1), "%s %s",
           ac->callsign[0] ? ac->callsign : ac->hex, ac->type);
  gfx.setTextColor(kColorLabel, kColorBackground);
  gfx.drawString(line1, left + 8, top + 5);

  char line2[28];
  snprintf(line2, sizeof(line2), "%s  %dkt", ac->alt,
           static_cast<int>(ac->gs_knots));
  gfx.setTextColor(kColorTagAltitude, kColorBackground);
  gfx.drawString(line2, left + 8, top + 24);
  gfx.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui::radar
