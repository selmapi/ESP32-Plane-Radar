// Coarse, best-effort per-IP rate limit.
//
// This is intentionally NOT durable: it's a plain in-memory Map scoped to
// one Worker isolate. Cloudflare may run many isolates concurrently (one per
// PoP, recycled over time), so this only bounds abuse from a single client
// hammering a single isolate -- it is not a global rate limit and assumes no
// paid product (Durable Objects / KV / Rate Limiting API) is available.
// Good enough at hobby scale for a public, read-only, cached map-blob proxy.

const WINDOW_MS = 60_000;
const MAX_REQUESTS_PER_WINDOW = 20;

interface Bucket {
  count: number;
  windowStart: number;
}

const buckets = new Map<string, Bucket>();

// Bound memory: if the map grows unreasonably large (many distinct IPs),
// sweep out expired buckets rather than leaking forever in a long-lived
// isolate. Sweep (not clear-all) so a burst of distinct IPs can't reset the
// counters of clients who are actually mid-window and about to be throttled.
const MAX_TRACKED_IPS = 10_000;

function sweepExpired(now: number, windowMs: number): void {
  for (const [ip, bucket] of buckets) {
    if (now - bucket.windowStart >= windowMs) {
      buckets.delete(ip);
    }
  }
}

export function isRateLimited(
  ip: string,
  now: number = Date.now(),
  maxPerWindow: number = MAX_REQUESTS_PER_WINDOW,
  windowMs: number = WINDOW_MS
): boolean {
  if (buckets.size > MAX_TRACKED_IPS) {
    sweepExpired(now, windowMs);
  }
  const existing = buckets.get(ip);
  if (!existing || now - existing.windowStart >= windowMs) {
    buckets.set(ip, { count: 1, windowStart: now });
    return false;
  }
  existing.count += 1;
  return existing.count > maxPerWindow;
}

/** Test-only: clear all tracked buckets between test cases. */
export function resetRateLimitState(): void {
  buckets.clear();
}
