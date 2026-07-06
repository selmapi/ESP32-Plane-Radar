// Way-stitching ported 1:1 from scripts/build_region_map.py's stitch_lines().
import { ekey } from "./geo";
import type { Line } from "./types";

/**
 * Merge ways sharing (quantized) endpoints into chains.
 *
 * OSM fragments roads/rivers into many short ways; each costs a fixed
 * span+endpoint floor in the emitted data. Joining fragments into real
 * chains is what makes the flash budget reachable. Closed rings pass
 * through unchanged.
 */
export function stitchLines(lines: Line[]): Line[] {
  const pool = new Map<number, Line>();
  lines.forEach((l, i) => pool.set(i, l.slice()));

  for (;;) {
    const endmap = new Map<string, number[]>();
    for (const [i, l] of pool) {
      const k0 = ekey(l[0]);
      const k1 = ekey(l[l.length - 1]);
      if (k0 === k1) {
        continue; // closed ring: leave as-is
      }
      if (!endmap.has(k0)) endmap.set(k0, []);
      endmap.get(k0)!.push(i);
      if (!endmap.has(k1)) endmap.set(k1, []);
      endmap.get(k1)!.push(i);
    }

    let joined = false;
    for (const [k, ids] of endmap) {
      const live = ids.filter((i) => pool.has(i));
      if (live.length < 2) {
        continue;
      }
      const a = live[0];
      const b = live[1];
      if (a === b) {
        continue;
      }
      let la = pool.get(a)!;
      let lb = pool.get(b)!;
      // Orient so la ends at k and lb starts at k (reverse as needed).
      if (ekey(la[0]) === k) {
        la = la.slice().reverse();
      }
      if (ekey(lb[lb.length - 1]) === k) {
        lb = lb.slice().reverse();
      }
      if (ekey(la[la.length - 1]) !== k || ekey(lb[0]) !== k) {
        continue; // stale entry from an earlier join this pass
      }
      pool.set(a, la.concat(lb.slice(1)));
      pool.delete(b);
      joined = true;
    }
    if (!joined) {
      return Array.from(pool.values());
    }
  }
}
