# V3 Map Service — Phase 1: Worker pipeline port

Phase 1 of the parked `2026-07-05-v3-map-service-design.md` spec. Scope:
**Worker only** — a deployable, tested Cloudflare Worker that ports
`scripts/build_region_map.py`'s pipeline to TypeScript and serves a binary
map blob. No firmware or app changes in this phase (those are Phase 2/3,
each landing as their own reviewed diff per the working agreement).

Defaults assumed (interactive confirmation was unavailable this session —
flag for correction):
- Worker code lives in-repo (`worker/`); deployment is left to Selma's own
  Cloudflare account (no credentials in this session).
- Trigger UX (explicit button, Phase 3) and the fact that no repartition is
  needed (spiffs partition is 0xE0000 = 896 KB, comfortably fits LittleFS +
  a ~25 KB blob) are recorded here but not implemented yet.

## Wire format (new — region_map.h has no binary form today)

The device parses this explicitly field-by-field (no raw struct casts —
wire layout must not depend on C++ struct padding/alignment). All multi-byte
fields little-endian.

```
offset  size  field
0       4     magic = "PRMB"
4       1     version = 1
5       3     reserved (zero)
8       4     centerLat (float32)
12      4     centerLon (float32)
16      2     vertCount (uint16)
18      2     spanCount (uint16)
20      2     townCount (uint16)
22      2     reserved (zero, pad to 4-byte boundary)
24      ...   verts:  vertCount * {int16 dlat, int16 dlon}                (4 B each)
...     ...   spans:  spanCount * {uint16 start, uint16 len, uint8 layer} (5 B each)
...     ...   towns:  townCount * {int16 dlat, int16 dlon, char label[5]} (9 B each)
```

Layer ids match `ui::radar::MapLayer` (0=Highway, 1=Water, 2=Boundary,
3=Town — Town has no spans, towns are the separate array above).
Quantization: `kMapQuantDeg = 1e-4` deg/unit, same as the baked format.

## Worker (`worker/`)

- `GET /map?lat=<f>&lon=<f>&radius=<km, default 80>` → `200
  application/octet-stream` binary blob per above, or `4xx` with a JSON
  error body.
- Pipeline ported 1:1 from `scripts/build_region_map.py`: Overpass fetch
  (primary + mirror, required `User-Agent`, per-layer query text unchanged),
  way stitching by quantized endpoint, river/lake noise filters, iterative
  Douglas-Peucker with the same 3-rung budget ladder, int16 quantization,
  interstates-only highway class (matches the 2026-07-04 owner decision).
- Cache via the Workers Cache API, keyed on rounded lat/lon (5 decimal
  places, matching Overpass cache key precision) + radius; TTL on the order
  of weeks (map data doesn't change fast enough to warrant more).
- Rate limiting: coarse per-IP limit is enough at hobby scale (documented
  as a TODO/simple counter — no paid Cloudflare add-on assumed).
- Input validation: radius clamped to a sane max (e.g. 200 km) to bound
  Overpass load and worst-case response size; lat/lon range-checked.
- No auth (spec's stated tradeoff: public read-only map-blob proxy).

## Tests

- Unit tests for stitching, Douglas-Peucker, quantization, and binary
  encoding — mirror the Python script's behavior on fixture inputs (same
  fixtures/expected byte output where practical, so the two pipelines can be
  diffed for parity).
- Worker request-handling tests with mocked Overpass responses (no live
  network calls in CI).

## Explicitly out of scope for Phase 1

- Device LittleFS mount, fetch/flash-swap, renderer source-of-truth switch.
- App "rebuild map" button, `/api/map/rebuild` endpoint, progress/error UI.
- Actual Worker deployment / wrangler secrets / DNS.
- NVS-configurable Worker URL override.
