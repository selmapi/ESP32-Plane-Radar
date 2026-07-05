# Plane Radar map service (V3 Phase 1)

A Cloudflare Worker that ports `scripts/build_region_map.py`'s OSM pipeline
(Overpass fetch → way stitching → water-noise filtering → iterative
Douglas-Peucker simplification → int16 quantization) to TypeScript, and
serves the result as a small binary blob over HTTP.

This is **Phase 1** of the parked
`docs/superpowers/specs/2026-07-05-v3-map-service-design.md` design (see
`docs/superpowers/plans/2026-07-05-v3-map-service-phase1.md` for the full
scope). This phase is Worker-only: no firmware or app changes, and nothing
here has been deployed to a live Cloudflare account.

## What it does

`GET /map?lat=<float>&lon=<float>&radius=<km, default 80, max 200>`

- Fetches four Overpass layers (highway, water, boundary, town) around the
  given center, exactly like the Python script (same queries, same
  `User-Agent` — Overpass's WAF 406-blocks default library user agents).
- Stitches OSM's fragmented ways into chains by shared (quantized) endpoint;
  closed rings pass through unchanged.
- Drops river chains shorter than 5 km and lake/pond outlines with a bbox
  diagonal under 2 km (matches `RIVER_MIN_KM` / `LAKE_MIN_DIAG_KM` in the
  Python source).
- Simplifies each polyline with an iterative (explicit-stack, not recursive)
  Douglas-Peucker, walking the same 3-rung tolerance ladder as the Python
  script (150 m, 300 m, 300 m-with-primary-dropped). Per the 2026-07-04 owner
  decision recorded in the Python source's `LADDER` comment, only
  `motorway` (interstate) roads are drawn regardless of rung — the
  `usePrimary` ladder flag is carried for parity but has no effect, same as
  upstream.
- Quantizes vertices to int16 offsets from the request center (`1e-4` deg =
  1 unit, clamped to the int16 range).
- Refuses to emit a near-empty map: if the highway or boundary layer parses
  to zero features from an otherwise-successful Overpass response, the
  request fails with `502` instead of silently returning junk.
- Encodes the result into the binary wire format below and returns it as
  `application/octet-stream`.

On invalid input (`lat`/`lon` out of range, missing, or non-numeric
`radius`) it returns `400` with a JSON `{ "error": "..." }` body. Upstream
Overpass failures or a still-oversized map at the final ladder rung return
`502` with the same JSON error shape.

## Wire format

See `docs/superpowers/plans/2026-07-05-v3-map-service-phase1.md` ("Wire
format" section) for the authoritative spec. Summary:

```
offset  size  field
0       4     magic = "PRMB"
4       1     version = 1
5       3     reserved (zero)
8       4     centerLat (float32, LE)
12      4     centerLon (float32, LE)
16      2     vertCount (uint16, LE)
18      2     spanCount (uint16, LE)
20      2     townCount (uint16, LE)
22      2     reserved (zero)
24      ...   verts:  vertCount * {int16 dlat, int16 dlon}                (4 B each)
...     ...   spans:  spanCount * {uint16 start, uint16 len, uint8 layer} (5 B each)
...     ...   towns:  townCount * {int16 dlat, int16 dlon, char label[5]} (9 B each)
```

All multi-byte fields are little-endian. The encoder (`src/encode.ts`) writes
every field explicitly via `DataView` — it never relies on a struct/memory
layout, so the device-side parser (also explicit, field-by-field) and this
Worker can never drift apart due to padding/alignment differences between
runtimes. Layer ids match `ui::radar::MapLayer` (0 = Highway, 1 = Water,
2 = Boundary, 3 = Town — towns have no spans; they're the separate array).

## Caching

Responses are cached with the Workers `caches.default` Cache API, keyed on
`lat`/`lon` rounded to 5 decimal places (~1.1 m, matching the Python script's
Overpass on-disk cache key precision) plus `radius`. `Cache-Control:
public, max-age=<seconds>` is set for a 3-week TTL — map data doesn't change
fast enough to warrant shorter, and this makes the Worker a much better
Overpass citizen than every device independently re-running the pipeline.

## Rate limiting

A coarse, best-effort per-IP limit (20 requests/minute) lives in
`src/rateLimit.ts` as a plain in-memory `Map`, scoped to a single Worker
isolate. This is **not** a durable or globally-consistent limit — Cloudflare
runs many isolates concurrently and recycles them — but it's enough to blunt
casual abuse at hobby scale without assuming a paid product (Durable
Objects, KV, or the Rate Limiting API). If this ever needs to be a real
global limit, swap in Durable Objects.

## Project layout

```
worker/
  src/
    types.ts      shared types (Point, Line, Overpass payload shapes, wire types)
    geo.ts         quantize, Douglas-Peucker, perpendicular distance, length/bbox helpers
    stitch.ts       way-stitching by quantized endpoint
    overpass.ts    Overpass QL query builders + fetch (primary + mirror, injectable fetch)
    pipeline.ts    fetch -> stitch -> filter -> ladder -> encode orchestration
    encode.ts      binary wire-format encoder (DataView-based)
    rateLimit.ts   in-memory per-IP rate limiter
    index.ts       Worker entrypoint / HTTP handler
  test/            vitest unit + integration tests (see below)
  wrangler.toml
  package.json
  tsconfig.json
```

## Running tests

```bash
cd worker
npm install
npm test          # vitest run
npm run typecheck # tsc --noEmit
```

### Why plain vitest, not `@cloudflare/vitest-pool-workers`

`@cloudflare/vitest-pool-workers` runs tests inside the actual `workerd`
runtime, which is the most faithful option but pulls in a real workerd
binary and Miniflare, and pins you to running everything (including plain
unit tests of pure functions) inside that runtime. Phase 1's logic is almost
entirely pure functions (geometry, stitching, encoding) plus one HTTP
handler; the handler was written to take its Cloudflare-specific
dependencies (`fetch`, the `Cache` object, and the current time) as
**injectable parameters** rather than reaching for the `caches` / global
`fetch` bindings directly. That means plain vitest, running in Node, can
exercise the exact same `handleRequest()` code path production traffic hits
— with a mocked `fetch` (`test/fixtures.ts`) standing in for Overpass and an
in-memory `FakeCache` (`test/fakeCache.ts`) standing in for
`caches.default` — without needing workerd at all. This keeps the suite
fast and the dependency tree small. If Phase 2+ adds Workers-specific
bindings (KV, Durable Objects, etc.) that are hard to fake faithfully,
revisit and consider migrating to `@cloudflare/vitest-pool-workers` then.

## Deploying

Deployment was **not** performed as part of this phase (no Cloudflare
credentials exist in the environment this was built in). To deploy from your
own Cloudflare account:

```bash
cd worker
npm install
npx wrangler login      # one-time, opens a browser to authorize your account
npx wrangler deploy     # or: npm run deploy
```

`wrangler.toml` has no bindings (no KV/D1/Durable Objects) — it's a
stateless Worker aside from the Cache API, which needs no binding. Update
`name` in `wrangler.toml` if you want a different subdomain/route.

## Known deviations from the Python script (and why)

- **Overpass caching**: the Python script caches raw per-layer JSON on disk
  under `scripts/cache/`. The Worker does not cache individual Overpass
  layer responses — only the final encoded blob, via the Workers Cache API.
  Per-layer caching would need a KV/R2 binding (a paid-tier-adjacent
  dependency this phase avoids); the final-blob cache already gives Overpass
  the traffic reduction that matters, since repeat requests for the same
  area (by far the common case) never re-hit Overpass at all.
- **Politeness delay**: matches the Python script — the four Overpass layer
  fetches are serialized with the same 2 s delay between calls
  (`OVERPASS_POLITENESS_DELAY_MS` in `src/pipeline.ts`, injectable for
  tests). The final-blob cache (3-week TTL) means this cost is paid at most
  once per `(lat, lon, radius)` per TTL window, not per device poll — an
  in-isolate single-flight guard (`src/index.ts`) also collapses concurrent
  first-time requests for the same key into one pipeline run.
- **`vertexBytes` budget accounting**: kept byte-for-byte identical to the
  Python `vertex_bytes()` helper (verts × 4 B + spans × 4 B — the per-span
  `layer` byte is not counted in the budget check, matching upstream, even
  though it is present in the actual emitted/encoded data).
- Everything else (query text, stitching algorithm, water-noise thresholds,
  DP tolerance ladder and rung order, interstates-only owner decision,
  quantization units/clamping, near-empty-map guards) is a direct, faithful
  port — see the corresponding module for a comment pointing back at the
  Python function it mirrors.
