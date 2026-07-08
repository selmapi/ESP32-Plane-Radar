#include "services/map_service.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>

#include <cstring>

#include "config.h"
#include "services/rebuild_guard.h"
#include "ui/region_map_blob.h"
#include "ui/region_map_source.h"

namespace services::map {

namespace {

constexpr char kPrefsNamespace[] = "map";
constexpr char kKeyUrl[] = "url";
constexpr size_t kUrlBufLen = 128;
constexpr int kConnectAttemptMs = 200;
// Generous vs. adsb_client's 10s. A cold Worker-side build (Overpass fetch +
// stitch) can take 37s-3min, but the Worker now responds with HTTP 202
// immediately on a cache miss and keeps building in the background (see
// kBuilding) -- so this timeout only needs to cover normal request/response
// latency, not the build itself. Cache hits measured ~1.3s on-device.
constexpr unsigned long kRequestTimeoutMs = 15000;
constexpr char kMapBinPath[] = "/map.bin";
constexpr char kMapBinTmpPath[] = "/map.bin.tmp";

char s_url[kUrlBufLen] = "";
PollFn s_poll_fn = nullptr;
bool s_rebuild_busy = false;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

bool isValidUrl(const char* url) {
  if (url == nullptr || url[0] == '\0') {
    return false;
  }
  return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

void setUrlBuf(const char* url) {
  strncpy(s_url, url, sizeof(s_url) - 1);
  s_url[sizeof(s_url) - 1] = '\0';
}

void persist(const char* url) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putString(kKeyUrl, url);
  prefs.end();
  setUrlBuf(url);
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

/** Streams the HTTP response body into `file`, polling during I/O (mirrors
 *  adsb_client.cpp's readResponseBodyWithPoll). Aborts (returns false) if the
 *  total bytes written would exceed kMapBlobMaxTotalBytes -- never let a
 *  broken/hostile response fill the flash partition unbounded. */
bool streamResponseBodyWithPoll(HTTPClient& http, File& file) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  uint8_t buffer[512];
  size_t total = 0;
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int available = stream->available();
    if (available > 0) {
      const int to_read = available > static_cast<int>(sizeof(buffer))
                               ? static_cast<int>(sizeof(buffer))
                               : available;
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes > 0) {
        total += static_cast<size_t>(read_bytes);
        if (total > ui::radar::kMapBlobMaxTotalBytes) {
          return false;
        }
        if (file.write(buffer, static_cast<size_t>(read_bytes)) !=
            static_cast<size_t>(read_bytes)) {
          return false;
        }
      }
    }
    if (content_length > 0 && total >= static_cast<size_t>(content_length)) {
      break;
    }
    if (!http.connected() && stream->available() <= 0) {
      break;
    }
    delay(1);
  }

  return total > 0;
}

void removeTmpIfExists() {
  if (LittleFS.exists(kMapBinTmpPath)) {
    LittleFS.remove(kMapBinTmpPath);
  }
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyUrl)) {
    const String stored = prefs.getString(kKeyUrl, "");
    if (isValidUrl(stored.c_str())) {
      setUrlBuf(stored.c_str());
    }
  }
  prefs.end();
  if (s_url[0] == '\0') {
    setUrlBuf(config::kDefaultMapServiceUrl);
  }
}

const char* serviceUrl() { return s_url; }

bool saveServiceUrl(const char* url) {
  if (!isValidUrl(url) || strlen(url) >= kUrlBufLen) {
    return false;
  }
  persist(url);
  Serial.printf("map: service URL saved: %s\n", url);
  return true;
}

const char* rebuildResultMessage(RebuildResult r) {
  switch (r) {
    case RebuildResult::kOk:
      return "ok";
    case RebuildResult::kNoUrlConfigured:
      return "no map service URL configured";
    case RebuildResult::kNetworkError:
      return "network error fetching map";
    case RebuildResult::kInvalidResponse:
      return "invalid map response";
    case RebuildResult::kWriteError:
      return "failed to write map to flash";
    case RebuildResult::kBuilding:
      return "map is building on the server -- try again shortly";
    case RebuildResult::kBusy:
      return "map rebuild already in progress";
  }
  return "unknown error";
}

RebuildResult rebuildForLocation(double lat, double lon, float radiusKm) {
  RebuildLock lock(s_rebuild_busy);
  if (!lock.acquired()) {
    Serial.println("map: rebuild already in progress -- rejecting nested call");
    return RebuildResult::kBusy;
  }

  if (s_url[0] == '\0') {
    return RebuildResult::kNoUrlConfigured;
  }

  String url = s_url;
  url += "/map?lat=";
  url += String(lat, 6);
  url += "&lon=";
  url += String(lon, 6);
  url += "&radius=";
  url += String(radiusKm, 1);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("map: http.begin failed");
    return RebuildResult::kNetworkError;
  }

  http.setTimeout(kRequestTimeoutMs);
  const int code = performGetWithPoll(http);
  if (code == HTTP_CODE_ACCEPTED) {
    // Cache miss: the Worker has started building in the background and
    // returned immediately with {"status":"building",...} instead of making
    // us hold the connection open for the multi-minute cold build.
    Serial.println("map: HTTP 202 -- building on server");
    http.end();
    return RebuildResult::kBuilding;
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("map: HTTP %d\n", code);
    http.end();
    return RebuildResult::kNetworkError;
  }

  removeTmpIfExists();
  File tmp = LittleFS.open(kMapBinTmpPath, "w");
  if (!tmp) {
    http.end();
    return RebuildResult::kWriteError;
  }

  const bool streamed = streamResponseBodyWithPoll(http, tmp);
  tmp.close();
  http.end();
  if (!streamed) {
    removeTmpIfExists();
    return RebuildResult::kInvalidResponse;
  }

  File check = LittleFS.open(kMapBinTmpPath, "r");
  if (!check) {
    removeTmpIfExists();
    return RebuildResult::kWriteError;
  }
  uint8_t header_buf[ui::radar::kMapBlobHeaderBytes];
  const bool header_read_ok =
      check.read(header_buf, sizeof(header_buf)) == sizeof(header_buf);
  const size_t file_size = check.size();
  check.close();

  ui::radar::MapBlobHeader header{};
  if (!header_read_ok || !ui::radar::decodeMapBlobHeader(header_buf, header)) {
    removeTmpIfExists();
    return RebuildResult::kInvalidResponse;
  }
  const size_t expected = ui::radar::mapBlobExpectedTotalBytes(header);
  if (expected > ui::radar::kMapBlobMaxTotalBytes || expected != file_size) {
    removeTmpIfExists();
    return RebuildResult::kInvalidResponse;
  }

  // LittleFS can't rename over (or reliably remove) a file that's still open;
  // the renderer holds /map.bin open for the process lifetime (see s_file in
  // region_map_source.cpp) so it can seek/read records on demand. Release it
  // as late as possible -- right before the swap -- so the old map stays
  // drawable through the entire (potentially long) streaming phase above.
  ui::radar::mapSourceRelease();

  // LittleFS's rename() overwrites an existing destination on its own, so
  // don't pre-remove kMapBinPath -- doing that unconditionally would leave
  // the device with no map at all (not the old one) if the rename below
  // then failed. Only fall back to remove-then-retry if a plain rename
  // doesn't work, keeping the old file intact for as long as possible.
  if (!LittleFS.rename(kMapBinTmpPath, kMapBinPath)) {
    if (LittleFS.exists(kMapBinPath)) {
      LittleFS.remove(kMapBinPath);
    }
    if (!LittleFS.rename(kMapBinTmpPath, kMapBinPath)) {
      removeTmpIfExists();
      // The old /map.bin (or nothing, if remove() above won) is still on
      // flash and intact -- re-attach so the renderer isn't left detached
      // (baked fallback) until reboot.
      ui::radar::mapSourceInit();
      return RebuildResult::kWriteError;
    }
  }

  ui::radar::mapSourceInit();
  return RebuildResult::kOk;
}

}  // namespace services::map
