#pragma once

// Indirection layer between the region-map renderer and its data: either the
// compile-time-baked arrays in region_map.h, or a runtime-fetched blob
// streamed from LittleFS (/map.bin) via region_map_blob.h. Never loads a
// fetched blob fully into RAM (ESP32-C3 heap budget) -- accessors seek+read
// only the records requested. Arduino/LittleFS-bound; not native-tested
// (region_map_blob.h carries the native-testable decode logic).

#include <cstdint>

#include "ui/region_map.h"

namespace ui::radar {

struct MapSourceInfo {
  float centerLat;
  float centerLon;
  uint16_t vertCount;
  uint16_t spanCount;
  uint16_t townCount;
  bool fromFile;  // false = baked arrays (region_map.h), true = /map.bin
};

// Probes /map.bin on LittleFS; validates it via region_map_blob.h. Falls
// back to the baked arrays (fromFile=false) if the file is absent or fails
// validation. Call once at boot, and again after a successful fetch/swap.
void mapSourceInit();

const MapSourceInfo& mapSourceInfo();

// index-bounds-checked against mapSourceInfo().spanCount/townCount; returns
// false (not found/out of range) rather than ever reading out of bounds.
bool mapSourceGetSpan(uint16_t index, MapSpan& out);
bool mapSourceGetTown(uint16_t index, MapTown& out);

// Reads up to `maxOut` consecutive verts starting at absolute index `start`
// into caller-owned `outBuf`. Returns the count actually read (may be less
// than maxOut at the end of the vert array); 0 on error/out-of-range start.
// Callers MUST loop/chunk for spans longer than their buffer, not assume
// one call reads a whole span.
uint16_t mapSourceGetVerts(uint16_t start, uint16_t maxOut, MapVert* outBuf);

}  // namespace ui::radar
