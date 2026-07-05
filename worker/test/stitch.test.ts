import { describe, expect, it } from "vitest";
import { stitchLines } from "../src/stitch";
import type { Line } from "../src/types";

describe("stitchLines", () => {
  it("joins two ways that share an endpoint into one chain", () => {
    const a: Line = [
      [0, 0],
      [1, 0],
    ];
    const b: Line = [
      [1, 0],
      [2, 0],
    ];
    const result = stitchLines([a, b]);
    expect(result).toHaveLength(1);
    expect(result[0]).toEqual([
      [0, 0],
      [1, 0],
      [2, 0],
    ]);
  });

  it("joins a multi-way chain in any input order", () => {
    const a: Line = [
      [2, 0],
      [3, 0],
    ];
    const b: Line = [
      [0, 0],
      [1, 0],
    ];
    const c: Line = [
      [1, 0],
      [2, 0],
    ];
    const result = stitchLines([a, b, c]);
    expect(result).toHaveLength(1);
    // The chain must contain exactly these 4 points in order along the
    // line -- but which end the join process started from (and therefore
    // whether the chain comes out forward or reversed) is an implementation
    // detail shared with the Python original, so accept either direction.
    const expected = [
      [0, 0],
      [1, 0],
      [2, 0],
      [3, 0],
    ];
    const forward = JSON.stringify(result[0]) === JSON.stringify(expected);
    const backward =
      JSON.stringify(result[0]) === JSON.stringify(expected.slice().reverse());
    expect(forward || backward).toBe(true);
  });

  it("orients ways correctly regardless of stored direction", () => {
    // b is stored reversed relative to how it should join a.
    const a: Line = [
      [0, 0],
      [1, 0],
    ];
    const bReversed: Line = [
      [2, 0],
      [1, 0],
    ];
    const result = stitchLines([a, bReversed]);
    expect(result).toHaveLength(1);
    expect(result[0]).toEqual([
      [0, 0],
      [1, 0],
      [2, 0],
    ]);
  });

  it("leaves a closed ring unchanged", () => {
    const ring: Line = [
      [0, 0],
      [1, 0],
      [1, 1],
      [0, 1],
      [0, 0],
    ];
    const result = stitchLines([ring]);
    expect(result).toHaveLength(1);
    expect(result[0]).toEqual(ring);
  });

  it("does not join ways that do not share an endpoint", () => {
    const a: Line = [
      [0, 0],
      [1, 0],
    ];
    const b: Line = [
      [5, 5],
      [6, 5],
    ];
    const result = stitchLines([a, b]);
    expect(result).toHaveLength(2);
  });

  it("returns an empty array for empty input", () => {
    expect(stitchLines([])).toEqual([]);
  });
});
