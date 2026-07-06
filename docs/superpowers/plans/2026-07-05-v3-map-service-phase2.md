# V3 Map Service — Phase 2+3: device integration + app button

Builds on Phase 1 (`2026-07-05-v3-map-service-phase1.md`, the Worker — PR #1).
This phase wires the device up to fetch, store, and render a personalized map
blob, and gives the phone app a way to trigger it. Written from a codebase
investigation (region map renderer, WiFiManager/web_app.cpp, NVS patterns,
adsb_client.cpp's HTTPS fetch idiom, native test scaffolding, webapp SPA) —
see prior session findings; not re-derived here.

## Key constraint: no full blob in RAM

The baked map (`kMapVerts`/`kMapSpans`/`kMapTowns`) costs zero RAM — it's
`const` data linked into flash/rodata. A *fetched* blob has no such luxury:
the wire format's own budget is 96 KB, but the ESP32-C3 has ~70 KB free heap
(CLAUDE.md). Loading a fetched blob fully into RAM to mirror the baked
arrays' access pattern is not viable at the budget ceiling.

**Decision**: store the fetched blob as a LittleFS file and access it via a
small streaming reader — read the 24-byte header once (cheap), then seek+read
only the verts/span/town records needed for whatever the renderer is drawing
right now, into a small fixed-size stack buffer. Sequential LittleFS reads on
ESP32 run well over 1 MB/s; a full ~25 KB typical blob read costs low-single-
digit milliseconds, and per-span reads are far smaller than that. No caching
layer needed for Phase 2 — reopen-and-seek per `drawRegionMap()` call is fine
given the CIC scope's render rate.

## New abstraction: `ui::radar::MapSource`

Introduces the indirection layer the renderer doesn't have today (today
`region_map_render.cpp` reads `kMapVerts`/`kMapSpans`/`kMapTowns` directly).

`include/ui/region_map_source.h` (declares; Arduino-bound `.cpp` — LittleFS
`File` — added to the Arduino build, NOT native `build_src_filter`):

```cpp
struct MapSourceInfo {
  float centerLat, centerLon;
  uint16_t vertCount, spanCount, townCount;
  bool fromFile;  // false = baked arrays, true = /map.bin
};

bool mapSourceInit();                 // call once at boot; probes /map.bin
const MapSourceInfo& mapSourceInfo();
bool mapSourceGetSpan(uint16_t index, MapSpan& out);
bool mapSourceGetVerts(uint16_t start, uint16_t count, MapVert* outBuf);  // count capped, caller loops
bool mapSourceGetTown(uint16_t index, MapTown& out);
```

When `/map.bin` is absent/invalid, all accessors fall through to the baked
`kMapVerts`/`kMapSpans`/`kMapTowns` arrays (today's behavior, unchanged).
`region_map_render.cpp` is rewired to call these accessors instead of the
raw externs; `mapCoversLocation` (already source-agnostic) is unchanged,
called with whichever center `mapSourceInfo()` reports.

## Wire format parsing (native-testable, Arduino-free)

`include/ui/region_map_blob.h` — header-only, mirrors `region_map_geom.h`'s
style (no Arduino/LittleFS includes) so it slots into `test/native/` with no
`build_src_filter` change:

```cpp
struct MapBlobHeader {
  float centerLat, centerLon;
  uint16_t vertCount, spanCount, townCount;
};
// Validates magic "PRMB", version == 1, and that headerBytes + counts*record
// sizes == totalLen exactly (rejects truncated/corrupt/oversized blobs).
bool parseMapBlobHeader(const uint8_t* data, size_t len, MapBlobHeader& out);

constexpr size_t kMapBlobHeaderBytes = 24;
constexpr size_t kMapBlobVertBytes = 4;
constexpr size_t kMapBlobSpanBytes = 5;
constexpr size_t kMapBlobTownBytes = 9;
// Decode one record at a known byte offset (bounds-checked by the caller
// using the header's counts).
MapVert decodeMapBlobVert(const uint8_t* data);
MapSpan decodeMapBlobSpan(const uint8_t* data);
MapTown decodeMapBlobTown(const uint8_t* data);
```

Byte-for-byte the mirror of the Worker's `worker/src/encode.ts` — same
struct, opposite direction. `region_map_source.cpp` uses this to validate on
load and to decode records on demand from the streamed file reads.

## Fetch / flash-swap: `src/services/map_service.{h,cpp}`

Mirrors `adsb_client.cpp`'s HTTPS idiom (HTTPClient + WiFiClientSecure +
`setInsecure()`, `pollNetwork()` during blocking I/O so WiFiManager's portal
keeps servicing per landmine #10) rather than inventing a new one:

```cpp
enum class MapFetchResult { kOk, kNetworkError, kInvalidResponse, kWriteError };
MapFetchResult rebuildMapForLocation(double lat, double lon, float radiusKm);
```

Sequence: GET `<mapServiceUrl>/map?lat=&lon=&radius=` → stream response body
directly to `/map.bin.tmp` on LittleFS (chunked, same 512-byte-buffer pattern
as `readResponseBodyWithPoll`) → validate header via `parseMapBlobHeader` on
the written file (magic/version/count-vs-filesize) → on success, atomically
rename `/map.bin.tmp` → `/map.bin` (LittleFS `rename()`; old `/map.bin` is
overwritten) → call `mapSourceInit()` again to pick up the new file. On any
failure, delete the `.tmp` file and leave the existing `/map.bin` (or baked
fallback) untouched — never leave a corrupt file at the final path.

**Blocking**: like the adsb.fi poll, this runs synchronously on the main loop
when triggered (landmine #10 — no separate task). A ~25 KB HTTPS fetch is a
few hundred ms to a few seconds depending on the Worker's Overpass cache
state; acceptable for an explicit, user-initiated action with a visible
"fetching..." state in the app, not something that runs every frame.

**LittleFS mount**: `LittleFS.begin(true /*formatOnFail*/)` once at boot in
`main.cpp` (mirrors nothing existing — this partition is untouched today).
No `platformio.ini` filesystem-upload tooling needed since the device writes
to it at runtime, not at build/flash time.

## Config: Worker URL

Compile-time default (`include/config.h`, e.g. `kDefaultMapServiceUrl`,
pointed at nothing/empty by default — this fork doesn't bake in a specific
person's Cloudflare Worker URL) + NVS override, following
`radar_location.cpp`'s exact pattern (own namespace `"map"`, `init()`/
`persist()`/getter, validated on load). Settings POST (`/api/settings` or a
new field) can set it from the app the same way lat/lon are set today.

## Web endpoint: `POST /api/map/rebuild`

New handler in `web_app.cpp`'s existing pattern (anonymous-namespace static
function + one `server.on(...)` line). Body: none needed (uses the device's
current configured lat/lon + a fixed default radius, matching "rebuild map
**for my location**" — not an arbitrary-location endpoint, avoiding a public
LAN-exposed way to make the device fetch arbitrary attacker-chosen URLs'
worth of Overpass data). Response: JSON `{ok: bool, error?: string}`. Add a
`mapSource` field (`"baked" | "fetched" | "hidden"`) to `/api/aircraft`'s
existing response (per the original spec) so the app can show current state
without polling a separate endpoint.

## App: `webapp/index.html`

Add a "Rebuild map for my location" button in the existing `#drawer`
LOCATION section (already has lat/lon inputs + `pushLocation()`, per the
codebase investigation). Button POSTs `/api/map/rebuild`, shows a
fetching/success/error state inline (small state var + text, following the
existing `state`/`syncDrawer()` read-into-DOM pattern — no new framework).
Rewrite the stale existing copy ("the baked CIC map stays put... reflash to
relocate it") since it becomes wrong once this ships.

## Native tests

`test/native/test_map_blob/test_map_blob.cpp`: valid header round-trip,
wrong magic rejected, wrong version rejected, truncated buffer rejected,
size-mismatch (counts imply more/less than actual `len`) rejected, vert/span/
town decode byte-exactness against hand-built fixtures (mirrors
`worker/test/encode.test.ts`'s fixtures where practical for cross-check).

## Explicitly deferred past this phase

- NVS-configurable Worker URL exposed in the WiFiManager *portal* itself
  (only the app/`/api/settings` path is in scope here) — portal form changes
  are a separate, smaller follow-up.
- Actual field test against a deployed Worker (needs Selma's Cloudflare
  account + her physical device — flashing requires her explicit go-ahead
  per the working agreement, never done unprompted).
