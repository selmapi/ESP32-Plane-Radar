import { beforeEach, describe, expect, it } from "vitest";
import { handleRequest, parseParams } from "../src/index";
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
});

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

  it("returns a 200 binary blob for a valid request", async () => {
    const ctx = new TestContext();
    const cache = new FakeCache();
    const req = new Request("https://worker.example/map?lat=40&lon=-105&radius=50");
    const res = await handleRequest(req, ctx, {
      fetchImpl: goodFetch,
      cache: cache as unknown as Cache,
      politenessDelayMs: 0,
    });
    await ctx.flush();

    expect(res.status).toBe(200);
    expect(res.headers.get("Content-Type")).toBe("application/octet-stream");
    expect(res.headers.get("Cache-Control")).toMatch(/max-age=/);
    const buf = new Uint8Array(await res.arrayBuffer());
    expect(buf.length).toBeGreaterThan(24); // header + some data
    expect(String.fromCharCode(buf[0], buf[1], buf[2], buf[3])).toBe("PRMB");
    expect(cache.size).toBe(1); // populated via waitUntil
  });

  it("serves the second identical request from cache without refetching", async () => {
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

  it("returns 502 when a mandatory layer parses to zero features", async () => {
    const ctx = new TestContext();
    const emptyHighwayFetch = makeMockFetch({
      highway: EMPTY_RESPONSE,
      water: WATER_RESPONSE,
      boundary: BOUNDARY_RESPONSE,
      town: TOWN_RESPONSE,
    });
    const req = new Request("https://worker.example/map?lat=40&lon=-105");
    const res = await handleRequest(req, ctx, {
      fetchImpl: emptyHighwayFetch,
      cache: new FakeCache() as unknown as Cache,
      politenessDelayMs: 0,
    });
    expect(res.status).toBe(502);
    const body = (await res.json()) as { error: string };
    expect(body.error).toMatch(/near-empty|highway/);
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
  });
});
