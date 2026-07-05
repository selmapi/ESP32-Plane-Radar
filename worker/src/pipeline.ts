// Full fetch -> stitch -> filter -> simplify -> quantize -> encode pipeline,
// ported 1:1 from scripts/build_region_map.py (build_layers / encode / the
// ladder loop in main()).
import { bboxDiagKm, douglasPeucker, lineLenKm, quantize } from "./geo";
import {
  fetchLayer,
  highwayLinesByClass,
  townNodes,
  waterLines,
  waysGeometry,
} from "./overpass";
import { stitchLines } from "./stitch";
import {
  LAYER_BOUNDARY,
  LAYER_HIGHWAY,
  LAYER_WATER,
  type EncodedLayers,
  type Line,
  type MapSpan,
  type OverpassResponse,
  type QuantVert,
  type Town,
} from "./types";
import { encodeMapBlob } from "./encode";

// Water noise filters (radar scale): keep stitched river chains only if the
// total length is >= 5 km; keep lake/pond outlines only if the bbox diagonal
// is >= 2 km. Smaller features are invisible at an 80 km radius display.
export const RIVER_MIN_KM = 5.0;
export const LAKE_MIN_DIAG_KM = 2.0;

// 96 KB budget: mirrors the device flash budget in build_region_map.py --
// the real per-frame draw-cost limit is unaffected by where the pipeline
// runs, so the Worker enforces the same ladder discipline.
export const FLASH_BUDGET_BYTES = 96 * 1024;

// Deterministic budget ladder: (DP tolerance meters, include primary roads).
// NOTE (owner decision, 2026-07-04, matches the Python "LADDER" comment):
// only interstates (motorway) are drawn regardless of rung -- trunk/primary
// roads read as noise at radar scale. The `usePrimary` flag is carried for
// parity with the Python source but has no effect, exactly like upstream.
export const LADDER: Array<{ tolM: number; usePrimary: boolean }> = [
  { tolM: 150.0, usePrimary: true },
  { tolM: 300.0, usePrimary: true },
  { tolM: 300.0, usePrimary: false },
];

export class NearEmptyMapError extends Error {}
export class BudgetExceededError extends Error {}

export interface BuiltLayers {
  hwByClass: Record<string, Line[]>;
  water: Line[];
  boundary: Line[];
  towns: Town[];
}

// Be polite to Overpass between layer fetches, matching the Python
// original's 2s inter-request sleep (CLAUDE.md landmine #7: overpass-api.de
// WAF-blocks aggressive/bursty callers). The Worker's own response cache
// means this per-request cost is paid at most once per (lat,lon,radius) per
// TTL window, not per device poll.
export const OVERPASS_POLITENESS_DELAY_MS = 2000;

function delay(ms: number): Promise<void> {
  if (ms <= 0) return Promise.resolve();
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/** Fetch, stitch, and filter all layers (Overpass fetch injectable for tests). */
export async function buildLayers(
  lat: number,
  lon: number,
  radiusM: number,
  fetchImpl: typeof fetch = fetch,
  politenessDelayMs: number = OVERPASS_POLITENESS_DELAY_MS
): Promise<BuiltLayers> {
  const kinds = ["highway", "water", "boundary", "town"] as const;
  const payloads: OverpassResponse[] = [];
  for (let i = 0; i < kinds.length; i++) {
    if (i > 0) {
      await delay(politenessDelayMs);
    }
    payloads.push(await fetchLayer(lat, lon, radiusM, kinds[i], fetchImpl));
  }
  const [highwayPayload, waterPayload, boundaryPayload, townPayload] = payloads;

  let hwByClass = highwayLinesByClass(highwayPayload);
  const [rivers, lakes] = waterLines(waterPayload);
  let boundary = waysGeometry(boundaryPayload);
  const towns = townNodes(townPayload);

  // A successful response with zero mandatory features means the query is
  // broken (e.g. tag scheme changed) -- fail loudly, don't emit an empty map.
  if (!Object.values(hwByClass).some((v) => v.length > 0)) {
    throw new NearEmptyMapError(
      "highway layer parsed to 0 features from a successful Overpass " +
        "response; the query or tag filter is broken -- refusing to emit " +
        "a near-empty map."
    );
  }
  if (boundary.length === 0) {
    throw new NearEmptyMapError(
      "boundary layer parsed to 0 features from a successful Overpass " +
        "response; the relation/way(r) query is broken -- refusing to " +
        "emit a near-empty map."
    );
  }

  hwByClass = Object.fromEntries(
    Object.entries(hwByClass).map(([c, v]) => [c, stitchLines(v)])
  );
  const stitchedRivers = stitchLines(rivers);
  const stitchedLakes = stitchLines(lakes);
  const keptRivers = stitchedRivers.filter((l) => lineLenKm(l) >= RIVER_MIN_KM);
  const keptLakes = stitchedLakes.filter((l) => bboxDiagKm(l) >= LAKE_MIN_DIAG_KM);
  boundary = stitchLines(boundary);

  return {
    hwByClass,
    water: [...keptRivers, ...keptLakes],
    boundary,
    towns,
  };
}

/** Simplify + quantize each layer into flat verts/spans arrays. */
export function encodeLayers(
  highway: Line[],
  water: Line[],
  boundary: Line[],
  lat: number,
  lon: number,
  tolDeg: number
): EncodedLayers {
  const verts: QuantVert[] = [];
  const spansByLayer: Record<number, MapSpan[]> = {
    [LAYER_HIGHWAY]: [],
    [LAYER_WATER]: [],
    [LAYER_BOUNDARY]: [],
  };
  const dropped: Record<number, number> = {
    [LAYER_HIGHWAY]: 0,
    [LAYER_WATER]: 0,
    [LAYER_BOUNDARY]: 0,
  };

  const layers: Array<[number, Line[]]> = [
    [LAYER_HIGHWAY, highway],
    [LAYER_WATER, water],
    [LAYER_BOUNDARY, boundary],
  ];
  for (const [layerId, lines] of layers) {
    for (const line of lines) {
      const simp = douglasPeucker(line, tolDeg);
      if (simp.length < 2) {
        dropped[layerId] += 1;
        continue;
      }
      const start = verts.length;
      for (const [py, px] of simp) {
        verts.push({ dlat: quantize(py, lat), dlon: quantize(px, lon) });
      }
      spansByLayer[layerId].push({ start, len: simp.length, layer: layerId });
    }
  }

  const spans = [
    ...spansByLayer[LAYER_HIGHWAY],
    ...spansByLayer[LAYER_WATER],
    ...spansByLayer[LAYER_BOUNDARY],
  ];
  return { verts, spans, dropped };
}

function vertexBytes(vertCount: number, spanCount: number): number {
  // 2x int16 per vertex (4 B) + 2x uint16 per span (4 B, matches Python's
  // vertex_bytes() -- the per-span uint8 layer byte is not counted there).
  return vertCount * 4 + spanCount * 4;
}

export interface PipelineResult {
  blob: Uint8Array;
  ladderRung: number;
  totalVertexBytes: number;
}

/**
 * Run the full pipeline for one request: fetch, stitch, filter, then walk
 * the budget ladder (interstates-only, per the owner decision) until a rung
 * fits FLASH_BUDGET_BYTES, and encode the final binary blob.
 */
export async function runPipeline(
  lat: number,
  lon: number,
  radiusKm: number,
  fetchImpl: typeof fetch = fetch,
  politenessDelayMs: number = OVERPASS_POLITENESS_DELAY_MS
): Promise<PipelineResult> {
  const radiusM = Math.round(radiusKm * 1000);
  const built = await buildLayers(lat, lon, radiusM, fetchImpl, politenessDelayMs);

  for (let rung = 0; rung < LADDER.length; rung++) {
    const { tolM } = LADDER[rung];
    // Owner decision (2026-07-04): interstates only on the scope regardless
    // of ladder rung -- see the LADDER comment above.
    const highway = built.hwByClass["motorway"] ?? [];
    const tolDeg = tolM / 111000.0;
    const { verts, spans } = encodeLayers(
      highway,
      built.water,
      built.boundary,
      lat,
      lon,
      tolDeg
    );
    const total = vertexBytes(verts.length, spans.length);
    if (total <= FLASH_BUDGET_BYTES || rung === LADDER.length - 1) {
      if (total > FLASH_BUDGET_BYTES) {
        throw new BudgetExceededError(
          `vertex data ${total} B exceeds ${FLASH_BUDGET_BYTES} B even at ` +
            "the final ladder rung; shrink radius or trim layers."
        );
      }
      const blob = encodeMapBlob({
        centerLat: lat,
        centerLon: lon,
        verts,
        spans,
        towns: built.towns,
      });
      return { blob, ladderRung: rung, totalVertexBytes: total };
    }
  }
  // Unreachable: the loop above always returns or throws on the last rung.
  throw new BudgetExceededError("ladder exhausted without a result");
}
