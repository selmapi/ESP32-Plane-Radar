// Geometry helpers ported 1:1 from scripts/build_region_map.py.
import type { Line, Point } from "./types";

/** int16 offset quantization: 1 unit = 1e-4 deg (matches the baked format). */
export const QUANT_DEG = 1e-4;

/** Quantized endpoint key (~1e-4 deg) for stitch matching. */
export function ekey(pt: Point): string {
  const a = Math.round(pt[0] / QUANT_DEG);
  const b = Math.round(pt[1] / QUANT_DEG);
  return `${a},${b}`;
}

/** Approximate polyline length in km (planar, cos-lat corrected). */
export function lineLenKm(line: Line): number {
  let total = 0;
  for (let i = 0; i < line.length - 1; i++) {
    const a = line[i];
    const b = line[i + 1];
    const dlat = (b[0] - a[0]) * 111.0;
    const dlon = (b[1] - a[1]) * 111.0 * Math.cos((a[0] * Math.PI) / 180);
    total += Math.hypot(dlat, dlon);
  }
  return total;
}

/** Approximate bbox diagonal in km (planar, cos-lat corrected). */
export function bboxDiagKm(line: Line): number {
  const lats = line.map((p) => p[0]);
  const lons = line.map((p) => p[1]);
  const dlat = (Math.max(...lats) - Math.min(...lats)) * 111.0;
  const dlon =
    (Math.max(...lons) - Math.min(...lons)) *
    111.0 *
    Math.cos((lats[0] * Math.PI) / 180);
  return Math.hypot(dlat, dlon);
}

/** Perpendicular distance of pt from segment a-b (degrees, planar). */
export function perpDist(pt: Point, a: Point, b: Point): number {
  const [py, px] = pt;
  const [ay, ax] = a;
  const [by, bx] = b;
  const dx = bx - ax;
  const dy = by - ay;
  if (dx === 0 && dy === 0) {
    return Math.hypot(px - ax, py - ay);
  }
  let t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
  t = Math.max(0.0, Math.min(1.0, t));
  const projx = ax + t * dx;
  const projy = ay + t * dy;
  return Math.hypot(px - projx, py - projy);
}

/**
 * Iterative Douglas-Peucker simplification (tolerance in degrees).
 *
 * Iterative (explicit stack) because stitched chains can run to thousands of
 * vertices -- recursion would risk a stack overflow.
 */
export function douglasPeucker(line: Line, tolDeg: number): Line {
  const n = line.length;
  if (n < 3) {
    return line.slice();
  }
  const keep = new Array<boolean>(n).fill(false);
  keep[0] = true;
  keep[n - 1] = true;
  const stack: Array<[number, number]> = [[0, n - 1]];
  while (stack.length > 0) {
    const [a, b] = stack.pop()!;
    if (b - a < 2) {
      continue;
    }
    let dmax = 0.0;
    let idx = a;
    for (let i = a + 1; i < b; i++) {
      const d = perpDist(line[i], line[a], line[b]);
      if (d > dmax) {
        dmax = d;
        idx = i;
      }
    }
    if (dmax > tolDeg) {
      keep[idx] = true;
      stack.push([a, idx]);
      stack.push([idx, b]);
    }
  }
  return line.filter((_, i) => keep[i]);
}

/** int16 offset units (1e-4 deg) from center, clamped to int16 range. */
export function quantize(valueDeg: number, centerDeg: number): number {
  const q = Math.round((valueDeg - centerDeg) / QUANT_DEG);
  return Math.max(-32768, Math.min(32767, q));
}
