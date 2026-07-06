#pragma once

// Wire-format decoder for the Phase 1 Worker's binary map blob. Header-only,
// Arduino/LittleFS-free (mirrors region_map_geom.h's style) so it is
// native-testable. Byte-for-byte mirror of worker/src/encode.ts -- see
// docs/superpowers/plans/2026-07-05-v3-map-service-phase1.md for the wire
// format. Reads are done field-by-field (no reinterpret_cast/struct-cast) so
// decoding never depends on this host's endianness or struct padding.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ui/region_map.h"

namespace ui::radar {

struct MapBlobHeader {
  float centerLat;
  float centerLon;
  uint16_t vertCount;
  uint16_t spanCount;
  uint16_t townCount;
};

constexpr size_t kMapBlobHeaderBytes = 24;
constexpr size_t kMapBlobVertBytes = 4;
constexpr size_t kMapBlobSpanBytes = 5;
constexpr size_t kMapBlobTownBytes = 9;
// Defensive ceiling independent of the Worker's own 96KB generation budget --
// rejects a corrupt/hostile header before any size arithmetic is trusted.
constexpr size_t kMapBlobMaxTotalBytes = 128 * 1024;

namespace detail {

inline uint16_t readU16le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline int16_t readI16le(const uint8_t* p) {
  return static_cast<int16_t>(readU16le(p));
}

inline float readF32le(const uint8_t* p) {
  uint32_t bits = static_cast<uint32_t>(p[0]) |
                  (static_cast<uint32_t>(p[1]) << 8) |
                  (static_cast<uint32_t>(p[2]) << 16) |
                  (static_cast<uint32_t>(p[3]) << 24);
  float out = 0.0f;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

}  // namespace detail

/**
 * Decodes the 24-byte header at `data` (caller guarantees at least 24 bytes
 * available). Returns false if magic or version don't match -- does NOT
 * validate size consistency (see mapBlobExpectedTotalBytes).
 */
inline bool decodeMapBlobHeader(const uint8_t* data, MapBlobHeader& out) {
  if (data[0] != 'P' || data[1] != 'R' || data[2] != 'M' || data[3] != 'B') {
    return false;
  }
  if (data[4] != 1) {
    return false;
  }
  // bytes 5..7 reserved, ignored.
  out.centerLat = detail::readF32le(data + 8);
  out.centerLon = detail::readF32le(data + 12);
  out.vertCount = detail::readU16le(data + 16);
  out.spanCount = detail::readU16le(data + 18);
  out.townCount = detail::readU16le(data + 20);
  // bytes 22..23 reserved, ignored.
  return true;
}

/**
 * Expected total blob size implied by a header's counts (24 + verts*4 +
 * spans*5 + towns*9). Caller compares this against the actual buffer/file
 * size to detect truncation or corruption.
 */
inline size_t mapBlobExpectedTotalBytes(const MapBlobHeader& header) {
  return kMapBlobHeaderBytes +
         static_cast<size_t>(header.vertCount) * kMapBlobVertBytes +
         static_cast<size_t>(header.spanCount) * kMapBlobSpanBytes +
         static_cast<size_t>(header.townCount) * kMapBlobTownBytes;
}

/** Decode one record at a byte offset the caller has already bounds-checked
 *  against the header's counts and the buffer/file length. */
inline MapVert decodeMapBlobVert(const uint8_t* data) {
  MapVert v{};
  v.dlat = detail::readI16le(data);
  v.dlon = detail::readI16le(data + 2);
  return v;
}

inline MapSpan decodeMapBlobSpan(const uint8_t* data) {
  MapSpan s{};
  s.start = detail::readU16le(data);
  s.len = detail::readU16le(data + 2);
  s.layer = data[4];
  return s;
}

inline MapTown decodeMapBlobTown(const uint8_t* data) {
  MapTown t{};
  t.dlat = detail::readI16le(data);
  t.dlon = detail::readI16le(data + 2);
  std::memcpy(t.label, data + 4, sizeof(t.label));
  return t;
}

}  // namespace ui::radar
