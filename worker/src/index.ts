// Cloudflare Worker entrypoint: GET /map?lat=<f>&lon=<f>&radius=<km>.
// See docs/superpowers/plans/2026-07-05-v3-map-service-phase1.md for the
// full spec this implements. Cache misses answer 202 + Retry-After and build
// the blob in the background (see the async-rebuild comment in
// handleRequest) -- the device client's 15 s HTTP timeout can never survive
// the 37 s+ cold pipeline, so a synchronous 200 was unreachable in practice.
import { OVERPASS_POLITENESS_DELAY_MS, runPipeline } from "./pipeline";
import { isRateLimited } from "./rateLimit";

export interface Env {
  // No bindings required for Phase 1 (no KV/D1/Durable Objects assumed).
}

const DEFAULT_RADIUS_KM = 80;
const MAX_RADIUS_KM = 200;
const MIN_RADIUS_KM = 1;

// Cache the finished blob for weeks -- map data doesn't change fast enough
// to warrant shorter TTLs, and this makes us a much better Overpass citizen
// than every device/user running the pipeline themselves.
const CACHE_TTL_SECONDS = 60 * 60 * 24 * 21; // 3 weeks

// How long a 202 tells the client to wait before polling again. Sized so a
// fast build (~37 s: 4 serialized Overpass layers + 3 x 2 s politeness
// delays) is usually done by the second poll, without hammering the Worker
// while Overpass is throttling and the build takes minutes.
const RETRY_AFTER_SECONDS = 45;

function jsonError(status: number, message: string): Response {
  return new Response(JSON.stringify({ error: message }), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

interface ParsedParams {
  lat: number;
  lon: number;
  radiusKm: number;
}

/** Validate & clamp query params. Returns an error Response, or the parsed params. */
export function parseParams(url: URL): ParsedParams | Response {
  const latStr = url.searchParams.get("lat");
  const lonStr = url.searchParams.get("lon");
  const radiusStr = url.searchParams.get("radius");

  if (latStr === null || lonStr === null) {
    return jsonError(400, "lat and lon query params are required");
  }

  const lat = Number(latStr);
  const lon = Number(lonStr);
  if (!Number.isFinite(lat) || lat < -90 || lat > 90) {
    return jsonError(400, "lat must be a finite number in [-90, 90]");
  }
  if (!Number.isFinite(lon) || lon < -180 || lon > 180) {
    return jsonError(400, "lon must be a finite number in [-180, 180]");
  }

  let radiusKm = DEFAULT_RADIUS_KM;
  if (radiusStr !== null) {
    const parsed = Number(radiusStr);
    if (!Number.isFinite(parsed) || parsed <= 0) {
      return jsonError(400, "radius must be a positive finite number (km)");
    }
    radiusKm = parsed;
  }
  // Clamp (not reject) to a sane range -- bounds Overpass load and worst-case
  // response size without punishing a slightly-too-large caller request.
  radiusKm = Math.max(MIN_RADIUS_KM, Math.min(MAX_RADIUS_KM, radiusKm));

  return { lat, lon, radiusKm };
}

function cacheKeyFor(url: URL, params: ParsedParams): Request {
  // Round to 5 decimal places (~1.1 m) matching the Python script's Overpass
  // cache key precision, so nearby requests share one cached blob.
  const latR = params.lat.toFixed(5);
  const lonR = params.lon.toFixed(5);
  const key = new URL(url.origin + "/map");
  key.searchParams.set("lat", latR);
  key.searchParams.set("lon", lonR);
  key.searchParams.set("radius", String(params.radiusKm));
  return new Request(key.toString(), { method: "GET" });
}

export interface HandleOptions {
  fetchImpl?: typeof fetch;
  cache?: Cache;
  now?: number;
  politenessDelayMs?: number;
}

interface MinimalContext {
  waitUntil(promise: Promise<unknown>): void;
}

// Single-flight guard: dedupes concurrent requests for the same cache key
// within one isolate so a thundering herd of first-time requests for the
// same location only runs the (slow, Overpass-hitting) pipeline once. Purely
// an in-isolate optimization -- it does not coordinate across isolates, the
// cache.match/put pair above still does that once the first flight completes.
// The stored promise is the FULL background chain (pipeline -> encode ->
// cache.put -> clear entry), never just the pipeline, so registering it with
// any request's ctx.waitUntil covers the cache write too.
const inFlight = new Map<string, Promise<void>>();

/** Test-only: clear the single-flight map (mirrors resetRateLimitState). */
export function resetInFlightState(): void {
  inFlight.clear();
}

export async function handleRequest(
  request: Request,
  ctx: MinimalContext,
  options: HandleOptions = {}
): Promise<Response> {
  const url = new URL(request.url);
  if (url.pathname !== "/map") {
    return jsonError(404, "not found; use GET /map?lat=&lon=&radius=");
  }
  if (request.method !== "GET") {
    return jsonError(405, "method not allowed; use GET");
  }

  const parsed = parseParams(url);
  if (parsed instanceof Response) {
    return parsed;
  }

  const ip = request.headers.get("CF-Connecting-IP") ?? "unknown";
  if (isRateLimited(ip, options.now)) {
    return jsonError(429, "rate limit exceeded; try again shortly");
  }

  const cache = options.cache ?? (caches as unknown as CacheStorage).default;
  const cacheKey = cacheKeyFor(url, parsed);

  const cached = await cache.match(cacheKey);
  if (cached) {
    return cached;
  }

  // Cache MISS: async rebuild contract. Do NOT await the pipeline in the
  // response path -- the device client times out at 15 s, while a cold build
  // takes 37 s minimum (4 serialized Overpass layers + 3 x 2 s politeness
  // delays) and multiple minutes when Overpass throttles Cloudflare egress
  // IPs (observed 2026-07-05: the same query answered in 2.8 s from a
  // residential IP but 504'd / took 2-3 min from the Worker). Instead, kick
  // off (or join) the background build and answer 202 + Retry-After
  // immediately; once the build lands in the cache, a later poll gets the
  // 200 blob via the cache.match above.
  const flightKey = cacheKey.url;
  let flight = inFlight.get(flightKey);
  if (!flight) {
    flight = runPipeline(
      parsed.lat,
      parsed.lon,
      parsed.radiusKm,
      options.fetchImpl,
      options.politenessDelayMs ?? OVERPASS_POLITENESS_DELAY_MS
    )
      .then(async (result) => {
        await cache.put(
          cacheKey,
          new Response(result.blob, {
            status: 200,
            headers: {
              "Content-Type": "application/octet-stream",
              "Cache-Control": `public, max-age=${CACHE_TTL_SECONDS}`,
              "X-Ladder-Rung": String(result.ladderRung),
            },
          })
        );
      })
      .catch(() => {
        // Swallow all pipeline failures (Overpass 5xx, near-empty map,
        // budget exceeded): there is deliberately NO persistent failure
        // state (no KV/DO). The finally below clears the in-flight entry,
        // so the client's next 202-driven poll starts a fresh attempt.
        // Swallowing here also keeps the shared chain from rejecting inside
        // waitUntil (an unhandled rejection at runtime / in tests).
      })
      .finally(() => {
        // Success or failure, drop the entry: after a successful cache.put
        // the cache.match above serves hits, and after a failure a retry
        // must be able to start over.
        inFlight.delete(flightKey);
      });
    inFlight.set(flightKey, flight);
  }

  // NOTE (docs vs. observed, checked 2026-07-06): Cloudflare's documented
  // contract (developers.cloudflare.com/workers/runtime-apis/context/) says
  // waitUntil() extends the invocation at most 30 SECONDS past the response,
  // with still-unsettled promises cancelled after that -- on paper, too
  // short for this 37 s+ (sometimes multi-minute) pipeline. Empirically
  // (2026-07-05, live service): invocations whose client disconnected
  // mid-build still ran to completion server-side and populated the cache.
  // We rely on that observed behavior, and hedge it: every 45 s client poll
  // re-registers the same in-flight chain on its own fresh ExecutionContext,
  // re-arming the extension window. If Cloudflare ever enforces the 30 s cap
  // strictly, a build would need several polls' worth of re-arming to
  // finish -- the 202 contract still converges, just more slowly.
  ctx.waitUntil(flight);

  return new Response(
    JSON.stringify({ status: "building", retryAfterSeconds: RETRY_AFTER_SECONDS }),
    {
      status: 202,
      headers: {
        "Content-Type": "application/json",
        "Retry-After": String(RETRY_AFTER_SECONDS),
      },
    }
  );
}

export default {
  async fetch(
    request: Request,
    _env: Env,
    ctx: ExecutionContext
  ): Promise<Response> {
    return handleRequest(request, ctx);
  },
};
