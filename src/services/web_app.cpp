#include "services/web_app.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>

#include "config.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/selection.h"
#include "ui/theme_manager.h"
#include "web/webapp_gz.h"

namespace services::web_app {

namespace {

// Mirrors ui::radar (radar_display.cpp) offsetKmFromCenter() exactly: flat
// 111 km/deg on both axes, no latitude scaling on the longitude term. The
// renderer does not apply cos(lat) correction anywhere in this codebase, so
// matching it (rather than "fixing" it here) keeps phone distances consistent
// with on-screen geometry.
constexpr float kKmPerDeg = 111.0f;

float distanceKm(float lat, float lon) {
  const float dx =
      static_cast<float>(lon - services::location::lon()) * kKmPerDeg;
  const float dy =
      static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  return sqrtf(dx * dx + dy * dy);
}

WebServer* s_server = nullptr;

void handleIndex() {
  s_server->sendHeader("Content-Encoding", "gzip");
  s_server->sendHeader("Cache-Control", "max-age=86400");
  s_server->send_P(200, "text/html",
                   reinterpret_cast<const char*>(web::kWebAppGz),
                   web::kWebAppGzLen);
}

void handleAircraft() {
  JsonDocument doc;
  doc["lat"] = services::location::lat();
  doc["lon"] = services::location::lon();
  doc["rangeIdx"] = ui::radar::rangeIndex();
  doc["theme"] = ui::radar::themeIndex();
  doc["useMiles"] = ui::radar::useMiles();
  doc["showRunways"] = ui::radar::showRunways();
  const char* sel = ui::radar::selectionHex();
  if (sel[0] != '\0') {
    doc["selected"] = sel;
  } else {
    doc["selected"] = nullptr;
  }

  JsonArray arr = doc["aircraft"].to<JsonArray>();
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* list = services::adsb::aircraftList();
  for (size_t i = 0; i < n; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["hex"] = list[i].hex;
    o["callsign"] = list[i].callsign;
    o["type"] = list[i].type;
    o["alt_ft"] = list[i].alt_ft;
    o["gs"] = list[i].gs_knots;
    o["vs_fpm"] = list[i].vs_fpm;
    o["squawk"] = list[i].squawk;
    o["emergency"] = list[i].emergency;
    o["lat"] = list[i].lat;
    o["lon"] = list[i].lon;
    o["track"] = list[i].track_deg;
    o["distance"] = distanceKm(list[i].lat, list[i].lon);
  }

  ui::radar::selectionNotePoll();  // a poll keeps any selection alive

  String out;
  serializeJson(doc, out);
  s_server->send(200, "application/json", out);
}

void handleSelect() {
  JsonDocument doc;
  if (deserializeJson(doc, s_server->arg("plain"))) {
    s_server->send(400, "application/json", R"({"error":"bad json"})");
    return;
  }
  if (doc["hex"].isNull()) {
    ui::radar::selectionSet(nullptr);
  } else {
    ui::radar::selectionSet(doc["hex"].as<const char*>());
  }
  s_server->send(200, "application/json", R"({"ok":true})");
}

void handleSettings() {
  JsonDocument doc;
  if (deserializeJson(doc, s_server->arg("plain"))) {
    s_server->send(400, "application/json", R"({"error":"bad json"})");
    return;
  }
  if (doc["theme"].is<int>()) {
    ui::radar::themeSet(static_cast<uint8_t>(doc["theme"].as<int>()));
  }
  if (doc["rangeIdx"].is<int>()) {
    const int want = doc["rangeIdx"].as<int>();
    // rangeNext() is the only mutator; cycle until we land on the target.
    for (int guard = 0; guard < static_cast<int>(ui::radar::kRangePresetCount) &&
                        ui::radar::rangeIndex() != want;
         ++guard) {
      ui::radar::rangeNext();
    }
  }
  if (doc["useMiles"].is<bool>()) {
    ui::radar::saveMilesFromPortal(doc["useMiles"].as<bool>() ? "T" : "");
  }
  if (doc["showRunways"].is<bool>()) {
    ui::radar::saveRunwaysFromPortal(doc["showRunways"].as<bool>() ? "T" : "");
  }
  // Phone app "Save location" sends {lat, lon} as JSON numbers. Reuse the
  // same validate+persist path as the WiFi portal (saveFromStrings), which
  // already clamps to [-90,90]/[-180,180] and writes through to NVS +
  // runtime s_lat/s_lon. Malformed/out-of-range input is skipped silently,
  // consistent with the other fields above.
  if ((doc["lat"].is<float>() || doc["lat"].is<int>()) &&
      (doc["lon"].is<float>() || doc["lon"].is<int>())) {
    char lat_buf[32];
    char lon_buf[32];
    snprintf(lat_buf, sizeof(lat_buf), "%.6f", doc["lat"].as<double>());
    snprintf(lon_buf, sizeof(lon_buf), "%.6f", doc["lon"].as<double>());
    if (!services::location::saveFromStrings(lat_buf, lon_buf)) {
      Serial.println("web_app: invalid lat/lon in /api/settings — ignored");
    }
  }
  s_server->send(200, "application/json", R"({"ok":true})");
}

void handleStatus() {
  JsonDocument doc;
  doc["uptime_s"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["heap"] = ESP.getFreeHeap();
  doc["ip"] = WiFi.localIP().toString();
  String out;
  serializeJson(doc, out);
  s_server->send(200, "application/json", out);
}

}  // namespace

void registerRoutes(WebServer& server) {
  s_server = &server;
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/aircraft", HTTP_GET, handleAircraft);
  server.on("/api/select", HTTP_POST, handleSelect);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/status", HTTP_GET, handleStatus);
  Serial.println("web_app: routes registered");
}

}  // namespace services::web_app
