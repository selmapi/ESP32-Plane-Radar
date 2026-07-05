// Cloudflare Worker entrypoint: GET /map?lat=<f>&lon=<f>&radius=<km>.
// See docs/superpowers/plans/2026-07-05-v3-map-service-phase1.md for the
// full spec this implements.
import {
  BudgetExceededError,
  NearEmptyMapError,
  OVERPASS_POLITENESS_DELAY_MS,
  runPipeline,
  type PipelineResult,
} from "./pipeline";
import { OverpassFetchError } from "./overpass";
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
const inFlight = new Map<string, Promise<PipelineResult>>();

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

  const flightKey = cacheKey.url;
  let result: PipelineResult;
  try {
    let flight = inFlight.get(flightKey);
    if (!flight) {
      flight = runPipeline(
        parsed.lat,
        parsed.lon,
        parsed.radiusKm,
        options.fetchImpl,
        options.politenessDelayMs ?? OVERPASS_POLITENESS_DELAY_MS
      );
      inFlight.set(flightKey, flight);
      // Cleanup chain is separate from the `await flight` below (which
      // handles the real error) -- catch here too, or an unhandled flight
      // rejection surfaces as an unhandled-rejection warning/crash.
      flight.finally(() => inFlight.delete(flightKey)).catch(() => {});
    }
    result = await flight;
  } catch (e) {
    if (e instanceof NearEmptyMapError) {
      return jsonError(502, `upstream data unusable: ${e.message}`);
    }
    if (e instanceof BudgetExceededError) {
      return jsonError(400, `map too large: ${e.message}`);
    }
    if (e instanceof OverpassFetchError) {
      return jsonError(502, `upstream fetch failed: ${e.message}`);
    }
    return jsonError(500, `internal error: ${String(e)}`);
  }

  const response = new Response(result.blob, {
    status: 200,
    headers: {
      "Content-Type": "application/octet-stream",
      "Cache-Control": `public, max-age=${CACHE_TTL_SECONDS}`,
      "X-Ladder-Rung": String(result.ladderRung),
    },
  });

  ctx.waitUntil(cache.put(cacheKey, response.clone()));
  return response;
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
