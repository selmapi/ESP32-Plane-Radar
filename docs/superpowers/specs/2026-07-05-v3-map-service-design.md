# Plane Radar V3 "Map Service" — Design Spec (DRAFT)

**Status:** DRAFT — parked until Selma green-lights v3. Written 2026-07-05 while
the v2.0.2 graceful-degradation patch shipped (map auto-hides when the
configured location is >100 km from the baked center).

## Problem

The CIC region map is baked at build time. Users who flash the prebuilt
release bin and set their location via the portal/app get either the demo map
(pre-v2.0.2: misleading) or no map (post-v2.0.2: safe but empty). Making the
map follow the app-configured lat/lon requires running the OSM pipeline
(fetch → stitch → filter → simplify → quantize) somewhere — the ESP32-C3
(~70 KB free heap vs ~60 MB raw OSM) can never do it locally.

## Proposal

A tiny **Cloudflare Worker** ("map service") runs the pipeline server-side;
the device fetches the finished ~25 KB blob and stores it in flash.

- **Worker** `GET /map?lat&lon&radius`: ports scripts/build_region_map.py's
  pipeline (Overpass fetch w/ proper User-Agent, way stitching, water filters,
  interstates-only default, Douglas-Peucker ladder, int16 quantization) to
  JS/TS; returns the exact binary layout region_map.h describes (magic +
  version header + center + counts + verts/spans/towns). Cache aggressively
  (Cloudflare Cache API keyed on rounded lat/lon+radius, TTL weeks) — makes
  us a better Overpass citizen than N users running scripts. Rate-limit;
  attribution header. Free tier is ample at hobby scale.
- **Device**: mount LittleFS on the unused SPIFFS partition slot; on "rebuild
  map" trigger, fetch from the Worker (HTTPS, chunked to flash, ~25 KB),
  validate magic/version/counts, atomically swap /map.bin; renderer prefers
  the file blob over the baked array when present and covering (reuse
  mapCoversLocation). Baked data remains the fallback (and the offline path).
- **App**: settings drawer gains "Rebuild map for my location" (POST
  /api/map/rebuild) with progress/error states; shows map source (baked /
  fetched / hidden) from /api/aircraft.
- **Config**: Worker URL as a compile-time default + NVS override via the
  portal (so forks can self-host).

## Open questions for the v3 brainstorm

1. Worker language: port pipeline to TS vs Python Workers — TS likely (Douglas-
   Peucker + stitching are ~200 lines; no numpy needed).
2. Who hosts the canonical Worker: Selma's CF account (cost ≈ $0) with the URL
   baked into releases, or docs-only self-host?
3. Trigger UX: automatic on location change (surprise network use) vs explicit
   button (recommended: explicit).
4. Partition: confirm the SPIFFS slot size in partitions/plane_radar.csv fits
   LittleFS + 25 KB blob; no repartition needed (repartition = breaking flash
   layout change for existing installs).
5. Security: the Worker is a public fetch proxy — cap radius/rate; no auth
   needed for read-only map blobs.

## Effort ballpark

Worker pipeline port + tests: the big half. Device LittleFS + fetch + swap +
renderer switch: medium. App button/states: small. README/API docs: small.
Roughly one V2-scale workstream (a day at V2 pace).
