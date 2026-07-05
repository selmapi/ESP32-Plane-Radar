// Overpass query construction + fetch, ported 1:1 from
// scripts/build_region_map.py (overpass_query / fetch).
import type { Line, OverpassElement, OverpassResponse, Point, Town } from "./types";
import { HIGHWAY_CLASSES } from "./types";

// Primary endpoint first, then a public mirror; tried in order per layer.
export const OVERPASS_URLS = [
  "https://overpass-api.de/api/interpreter",
  "https://overpass.kumi.systems/api/interpreter",
];

// Required: overpass-api.de's WAF 406-blocks requests with default library
// User-Agent strings -- do not remove this header.
export const USER_AGENT =
  "PlaneRadarMapGen/1.0 (+https://github.com/selmapi/ESP32-Plane-Radar)";

export type OverpassKind = "highway" | "water" | "boundary" | "town";

/** Overpass QL for one layer, around(radius, lat, lon). */
export function overpassQuery(
  lat: number,
  lon: number,
  radiusM: number,
  kind: OverpassKind
): string {
  const around = `(around:${radiusM},${lat},${lon})`;
  switch (kind) {
    case "highway":
      return (
        "[out:json][timeout:120];" +
        "(" +
        `way["highway"~"^(motorway|trunk|primary)$"]${around};` +
        ");out geom;"
      );
    case "water":
      return (
        "[out:json][timeout:120];" +
        "(" +
        `way["waterway"="river"]${around};` +
        `way["natural"="water"]${around};` +
        ");out geom;"
      );
    case "boundary":
      // County lines are relations, not tagged ways: fetch the member ways
      // of admin_level=6 relations (way(r)) with geometry.
      return (
        "[out:json][timeout:120];" +
        `relation["boundary"="administrative"]["admin_level"="6"]${around};` +
        "way(r);" +
        "out geom;"
      );
    case "town":
      return (
        "[out:json][timeout:120];" +
        "(" +
        `node["place"~"^(town|city)$"]${around};` +
        ");out;"
      );
  }
}

export class OverpassFetchError extends Error {}

/**
 * Fetch one Overpass layer. Tries the primary endpoint, then the mirror.
 * `fetchImpl` is injectable so tests never make live network calls.
 */
export async function fetchLayer(
  lat: number,
  lon: number,
  radiusM: number,
  kind: OverpassKind,
  fetchImpl: typeof fetch = fetch
): Promise<OverpassResponse> {
  const query = overpassQuery(lat, lon, radiusM, kind);
  const body = new URLSearchParams({ data: query }).toString();
  let lastErr: unknown;
  for (const url of OVERPASS_URLS) {
    try {
      const res = await fetchImpl(url, {
        method: "POST",
        headers: {
          "User-Agent": USER_AGENT,
          "Content-Type": "application/x-www-form-urlencoded",
        },
        body,
      });
      if (!res.ok) {
        lastErr = new Error(`HTTP ${res.status} from ${url}`);
        continue;
      }
      return (await res.json()) as OverpassResponse;
    } catch (e) {
      lastErr = e;
    }
  }
  throw new OverpassFetchError(
    `Overpass fetch failed for '${kind}' on all endpoints ` +
      `(${OVERPASS_URLS.join(", ")}): ${String(lastErr)}`
  );
}

/** Extract each way's [(lat, lon), ...] geometry. */
export function waysGeometry(payload: OverpassResponse): Line[] {
  const lines: Line[] = [];
  for (const el of payload.elements ?? []) {
    const geom = el.geometry;
    if (!geom || geom.length === 0) continue;
    const line: Line = geom.map((pt) => [pt.lat, pt.lon] as Point);
    if (line.length >= 2) lines.push(line);
  }
  return lines;
}

/** Split highway ways by class so the ladder can drop primary roads. */
export function highwayLinesByClass(
  payload: OverpassResponse
): Record<string, Line[]> {
  const out: Record<string, Line[]> = {};
  for (const c of HIGHWAY_CLASSES) out[c] = [];
  for (const el of payload.elements ?? []) {
    const geom = el.geometry;
    if (!geom || geom.length === 0) continue;
    const cls = el.tags?.highway ?? "";
    if (!(cls in out)) continue;
    const line: Line = geom.map((pt) => [pt.lat, pt.lon] as Point);
    if (line.length >= 2) out[cls].push(line);
  }
  return out;
}

/** Split water ways into river centerlines and lake/pond outlines. */
export function waterLines(payload: OverpassResponse): [Line[], Line[]] {
  const rivers: Line[] = [];
  const lakes: Line[] = [];
  for (const el of payload.elements ?? []) {
    const geom = el.geometry;
    if (!geom || geom.length === 0) continue;
    const line: Line = geom.map((pt) => [pt.lat, pt.lon] as Point);
    if (line.length < 2) continue;
    const tags = el.tags ?? {};
    if (tags.waterway === "river") {
      rivers.push(line);
    } else if (tags.natural === "water") {
      lakes.push(line);
    }
  }
  return [rivers, lakes];
}

/** Extract (lat, lon, <=4-char label) for town/city nodes. */
export function townNodes(payload: OverpassResponse): Town[] {
  const towns: Town[] = [];
  for (const el of payload.elements ?? []) {
    if (el.type !== "node") continue;
    const name = el.tags?.name ?? "";
    if (!name) continue;
    const parts = name.split(/\s+/).filter(Boolean);
    let label: string;
    if (parts.length >= 2) {
      label = parts
        .map((p) => p[0])
        .join("")
        .slice(0, 4)
        .toUpperCase();
    } else {
      label = name.slice(0, 4).toUpperCase();
    }
    towns.push({ lat: el.lat as number, lon: el.lon as number, label });
  }
  return towns;
}

export type { OverpassElement };
