import { beforeEach, describe, expect, it } from "vitest";
import { handleRequest, parseParams, resetInFlightState } from "../src/index";
import { resetRateLimitState } from "../src/rateLimit";
import { FakeCache } from "./fakeCache";
import {
  BOUNDARY_RESPONSE,
  EMPTY_RESPONSE,
  HIGHWAY_RESPONSE,
  TOWN_RESPONSE,
  WATER_RESPONSE,
  makeMockFetch,
} from "./fixtures";

class TestContext {
  waited: Promise<unknown>[] = [];
  waitUntil(p: Promise<unknown>): void {
    this.waited.push(p);
  }
  async flush(): Promise<void> {
    await Promise.all(this.waited);
  }
}

beforeEach(() => {
  resetRateLimitState();
  // The single-flight map is module-level state; clear it so a background
  // build left pending by one test can't be joined by the next test (whose
  // FakeCache is a different object).
  resetInFlightState();
});

async function expectBuilding202(res: Response): Promise<void> {
  expect(res.status).toBe(202);
  expect(res.headers.get("Content-Type")).toBe("application/json");
  expect(res.headers.get("Retry-After")).toBe("45");
  const body = (await res.json()) as { status: string; retryAfterSeconds: number };
  expect(body).toEqual({ status: "building", retryAfterSeconds: 45 });
}

describe("parseParams", () => {
  it("accepts valid lat/lon/radius", () => {
    const url = new URL("https://example.com/map?lat=40&lon=-105&radius=50");
    const result = parseParams(url);
    expect(result).toEqual({ lat: 40, lon: -105, radiusKm: 50 });
  });

  it("defaults radius to 80 when omitted", () => {
    const url = new URL("https://example.com/map?lat=40&lon=-105");
    const result = parseParams(url);
    expect(result).toEqual({ lat: 40, lon: -105, radiusKm: 80 });
  });

  it("clamps radius to the 200 km max", () => {
    const url = new URL("https://example.com/map?lat=40&lon=-105&radius=9999");
    const result = parseParams(url);
    expect(result).toEqual({ lat: 40, lon: -105, radiusKm: 200 });
  });

  it("rejects out-of-range lat", async () => {
    const url = new URL("https://example.com/map?lat=95&lon=-105");
    const result = parseParams(url);
    expect(result).toBeInstanceOf(Response);
    expect((result as Response).status).toBe(400);
  });

  it("rejects out-of-range lon", async () => {
    const url = new URL("https://example.com/map?lat=40&lon=200");
    const result = parseParams(url);
    expect(result).toBeInstanceOf(Response);
    expect((result as Response).status).toBe(400);
  });

  it("rejects missing lat/lon", () => {
    const url = new URL("https://example.com/map");
    const result = parseParams(url);
    expect(result).toBeInstanceOf(Response);
    expect((result as Response).status).toBe(400);
  });

  it("rejects non-numeric radius", () => {
    const url = new URL("https://example.com/map?lat=40&lon=-105&radius=abc");
    const result = parseParams(url);
    expect(result).toBeInstanceOf(Response);
    expect((result as Response).status).toBe(400);
  });
});

describe("handleRequest", () => {
  const goodFetch = makeMockFetch({
    highway: HIGHWAY_RESPONSE,
    water: WATER_RESPONSE,
    boundary: BOUNDARY_RESPONSE,
    town: TOWN_RESPONSE,
  });

  it("returns 202 building on a cache miss, then 200 with the blob once the background build lands", async () => {
    const ctx = new TestContext();
    const cache = new FakeCache();
    const req = new Request("https://worker.example/map?lat=40&lon=-105&radius=50");
    const res = await handleRequest(req, ctx, {
      fetchImpl: goodFetch,
      cache: cache as unknown as Cache,
      politenessDelayMs: 0,
    });

    // Immediate response: 202 building, nothing in the cache yet.
    await expectBuilding202(res);
    expect(ctx.waited.length).toBeGreaterThan(0); // build registered via waitUntil

    // Await the captured waitUntil promise -- the background build must have
    // populated the cache by the time it settles.
    await ctx.flush();
    expect(cache.size).toBe(1);

    // A later poll (fresh context, as in production) gets the 200 blob.
    const ctx2 = new TestContext();
    const res2 = await handleRequest(req.clone(), ctx2, {
      fetchImpl: goodFetch,
      cache: cache as unknown as Cache,
      politenessDelayMs: 0,
    });
    expect(res2.status).toBe(200);
    expect(res2.headers.get("Content-Type")).toBe("application/octet-stream");
    expect(res2.headers.get("Cache-Control")).toMatch(/max-age=/);
    const buf = new Uint8Array(await res2.arrayBuffer());
    expect(buf.length).toBeGreaterThan(24); // header + some data
    expect(String.fromCharCode(buf[0], buf[1], buf[2], buf[3])).toBe("PRMB");
  });

  it("gives 202 to a request arriving while the same key is in flight, without a second pipeline run", async () => {
    const ctx = new TestContext();
    const cache = new FakeCache();
    let fetchCalls = 0;
    let releaseGate!: () => void;
    const gate = new Promise<void>((resolve) => {
      releaseGate = resolve;
    });
    // Hold the pipeline's first Overpass fetch open so the flight is still
    // pending when the second request arrives.
    const gatedFetch: typeof fetch = async (url, init) => {
      fetchCalls++;
      await gate;
      return goodFetch(url as string, init);
    };
    const opts = {
      fetchImpl: gatedFetch,
      cache: cache as unknown as Cache,
      politenessDelayMs: 0,
    };
    const req = new Request("https://worker.example/map?lat=40&lon=-105&radius=50");

    const res1 = await handleRequest(req.clone(), ctx, opts);
    await expectBuilding202(res1);
    const res2 = await handleRequest(req.clone(), ctx, opts);
    await expectBuilding202(res2);
    expect(fetchCalls).toBe(1); // single flight: only one pipeline, stuck on layer 1

    releaseGate();
    await ctx.flush();
    expect(cache.size).toBe(1); // exactly one blob cached

    const res3 = await handleRequest(req.clone(), new TestContext(), opts);
    expect(res3.status).toBe(200);
  });

  it("serves a repeat request from cache without refetching", async () => {
    const ctx = new TestContext();
    const cache = new FakeCache();
    let fetchCalls = 0;
    const countingFetch: typeof fetch = async (url, init) => {
      fetchCalls++;
      return goodFetch(url as string, init);
    };
    const req1 = new Request("https://worker.example/map?lat=40&lon=-105&radius=50");
    await handleRequest(req1, ctx, { fetchImpl: countingFetch, cache: cache as unknown as Cache, politenessDelayMs: 0 });
    await ctx.flush();
    const callsAfterFirst = fetchCalls;
    expect(callsAfterFirst).toBeGreaterThan(0);

    const req2 = new Request("https://worker.example/map?lat=40&lon=-105&radius=50");
    const res2 = await handleRequest(req2, ctx, {
      fetchImpl: countingFetch,
      cache: cache as unknown as Cache,
      politenessDelayMs: 0,
    });
    expect(res2.status).toBe(200);
    expect(fetchCalls).toBe(callsAfterFirst); // no new upstream calls
  });

  it("returns 400 for invalid query params before touching cache or fetch", async () => {
    const ctx = new TestContext();
    const cache = new FakeCache();
    const req = new Request("https://worker.example/map?lat=999&lon=-105");
    const res = await handleRequest(req, ctx, {
      fetchImpl: goodFetch,
      cache: cache as unknown as Cache,
    });
    expect(res.status).toBe(400);
    const body = (await res.json()) as { error: string };
    expect(body.error).toMatch(/lat/);
    expect(cache.size).toBe(0);
  });

  it("returns 404 for unknown paths", async () => {
    const ctx = new TestContext();
    const req = new Request("https://worker.example/other");
    const res = await handleRequest(req, ctx, { cache: new FakeCache() as unknown as Cache });
    expect(res.status).toBe(404);
  });

  it("returns 405 for non-GET methods", async () => {
    const ctx = new TestContext();
    const req = new Request("https://worker.example/map?lat=40&lon=-105", {
      method: "POST",
    });
    const res = await handleRequest(req, ctx, { cache: new FakeCache() as unknown as Cache });
    expect(res.status).toBe(405);
  });

  it("clears the in-flight entry on pipeline failure so a later poll re-runs the pipeline", async () => {
    const cache = new FakeCache();
    let fetchCalls = 0;
    const emptyHighwayFetch = makeMockFetch({
      highway: EMPTY_RESPONSE, // mandatory layer parses to zero features -> pipeline throws
      water: WATER_RESPONSE,
      boundary: BOUNDARY_RESPONSE,
      town: TOWN_RESPONSE,
    });
    const countingFailFetch: typeof fetch = async (url, init) => {
      fetchCalls++;
      return emptyHighwayFetch(url as string, init);
    };
    const opts = {
      fetchImpl: countingFailFetch,
      cache: cache as unknown as Cache,
      politenessDelayMs: 0,
    };
    const req = new Request("https://worker.example/map?lat=40&lon=-105");

    // First attempt: 202 immediately; the background build then fails.
    const ctx1 = new TestContext();
    const res1 = await handleRequest(req.clone(), ctx1, opts);
    await expectBuilding202(res1);
    await ctx1.flush(); // background failure must not reject the waitUntil promise
    expect(cache.size).toBe(0); // nothing cached on failure
    const callsAfterFirst = fetchCalls;
    expect(callsAfterFirst).toBeGreaterThan(0);

    // The failed flight must have been cleared: a retry poll starts a fresh
    // pipeline run (new upstream calls) and gets another 202, not an error.
    const ctx2 = new TestContext();
    const res2 = await handleRequest(req.clone(), ctx2, opts);
    await expectBuilding202(res2);
    await ctx2.flush();
    expect(fetchCalls).toBeGreaterThan(callsAfterFirst);
    expect(cache.size).toBe(0);
  });

  it("rate-limits a client after too many requests in one window", async () => {
    const ctx = new TestContext();
    const cache = new FakeCache();
    const now = 1_000_000;
    let lastRes;
    for (let i = 0; i < 25; i++) {
      const req = new Request("https://worker.example/map?lat=40&lon=-105", {
        headers: { "CF-Connecting-IP": "1.2.3.4" },
      });
      lastRes = await handleRequest(req, ctx, {
        fetchImpl: goodFetch,
        cache,
        now,
        politenessDelayMs: 0,
      });
    }
    expect(lastRes!.status).toBe(429);
    await ctx.flush(); // settle the background build started by the first request
  });
});
