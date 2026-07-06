#include "ui/region_map_source.h"

#include <LittleFS.h>

#include "ui/region_map.h"
#include "ui/region_map_blob.h"

namespace ui::radar {

namespace {

MapSourceInfo s_info = {
    kMapCenterLat, kMapCenterLon,       static_cast<uint16_t>(kMapVertCount),
    static_cast<uint16_t>(kMapSpanCount), static_cast<uint16_t>(kMapTownCount),
    false,
};

File s_file;

void resetToBaked() {
  s_info.centerLat = kMapCenterLat;
  s_info.centerLon = kMapCenterLon;
  s_info.vertCount = static_cast<uint16_t>(kMapVertCount);
  s_info.spanCount = static_cast<uint16_t>(kMapSpanCount);
  s_info.townCount = static_cast<uint16_t>(kMapTownCount);
  s_info.fromFile = false;
}

}  // namespace

void mapSourceInit() {
  if (s_file) {
    s_file.close();
  }
  resetToBaked();

  File f = LittleFS.open("/map.bin", "r");
  if (!f) {
    return;  // no file -- baked fallback already set above
  }

  uint8_t header_buf[kMapBlobHeaderBytes];
  if (f.read(header_buf, sizeof(header_buf)) != sizeof(header_buf)) {
    f.close();
    return;
  }
  MapBlobHeader header{};
  if (!decodeMapBlobHeader(header_buf, header)) {
    f.close();
    return;
  }
  const size_t expected = mapBlobExpectedTotalBytes(header);
  if (expected > kMapBlobMaxTotalBytes ||
      expected != static_cast<size_t>(f.size())) {
    f.close();
    return;
  }

  s_info.centerLat = header.centerLat;
  s_info.centerLon = header.centerLon;
  s_info.vertCount = header.vertCount;
  s_info.spanCount = header.spanCount;
  s_info.townCount = header.townCount;
  s_info.fromFile = true;
  s_file = f;  // keep open for the process lifetime; subsequent reads seek
}

const MapSourceInfo& mapSourceInfo() { return s_info; }

bool mapSourceGetSpan(uint16_t index, MapSpan& out) {
  if (!s_info.fromFile) {
    if (index >= kMapSpanCount) {
      return false;
    }
    out = kMapSpans[index];
    return true;
  }
  if (index >= s_info.spanCount) {
    return false;
  }
  const uint32_t offset =
      kMapBlobHeaderBytes +
      static_cast<uint32_t>(s_info.vertCount) * kMapBlobVertBytes +
      static_cast<uint32_t>(index) * kMapBlobSpanBytes;
  if (!s_file.seek(offset)) {
    return false;
  }
  uint8_t buf[kMapBlobSpanBytes];
  if (s_file.read(buf, sizeof(buf)) != sizeof(buf)) {
    return false;
  }
  out = decodeMapBlobSpan(buf);
  return true;
}

bool mapSourceGetTown(uint16_t index, MapTown& out) {
  if (!s_info.fromFile) {
    if (index >= kMapTownCount) {
      return false;
    }
    out = kMapTowns[index];
    return true;
  }
  if (index >= s_info.townCount) {
    return false;
  }
  const uint32_t offset =
      kMapBlobHeaderBytes +
      static_cast<uint32_t>(s_info.vertCount) * kMapBlobVertBytes +
      static_cast<uint32_t>(s_info.spanCount) * kMapBlobSpanBytes +
      static_cast<uint32_t>(index) * kMapBlobTownBytes;
  if (!s_file.seek(offset)) {
    return false;
  }
  uint8_t buf[kMapBlobTownBytes];
  if (s_file.read(buf, sizeof(buf)) != sizeof(buf)) {
    return false;
  }
  out = decodeMapBlobTown(buf);
  return true;
}

uint16_t mapSourceGetVerts(uint16_t start, uint16_t maxOut, MapVert* outBuf) {
  if (maxOut == 0 || outBuf == nullptr) {
    return 0;
  }

  if (!s_info.fromFile) {
    if (start >= kMapVertCount) {
      return 0;
    }
    uint16_t count = maxOut;
    if (static_cast<uint32_t>(start) + count > kMapVertCount) {
      count = static_cast<uint16_t>(kMapVertCount - start);
    }
    for (uint16_t i = 0; i < count; ++i) {
      outBuf[i] = kMapVerts[start + i];
    }
    return count;
  }

  if (start >= s_info.vertCount) {
    return 0;
  }
  uint16_t count = maxOut;
  if (static_cast<uint32_t>(start) + count > s_info.vertCount) {
    count = static_cast<uint16_t>(s_info.vertCount - start);
  }
  const uint32_t offset =
      kMapBlobHeaderBytes + static_cast<uint32_t>(start) * kMapBlobVertBytes;
  if (!s_file.seek(offset)) {
    return 0;
  }
  uint16_t read_count = 0;
  for (uint16_t i = 0; i < count; ++i) {
    uint8_t buf[kMapBlobVertBytes];
    if (s_file.read(buf, sizeof(buf)) != sizeof(buf)) {
      break;  // never trust a partial read as valid data
    }
    outBuf[i] = decodeMapBlobVert(buf);
    ++read_count;
  }
  return read_count;
}

}  // namespace ui::radar
