import { describe, expect, it } from "vitest";
import { douglasPeucker, quantize } from "../src/geo";
import type { Line } from "../src/types";

describe("quantize", () => {
  it("converts degree offsets to 1e-4 deg int units", () => {
    expect(quantize(1.0001, 1.0)).toBe(1);
    expect(quantize(1.0, 1.0)).toBe(0);
    expect(quantize(0.9999, 1.0)).toBe(-1);
  });

  it("rounds to nearest unit", () => {
    // 0.00005 deg = 0.5 units from center -> rounds to nearest even-ish (JS
    // Math.round rounds .5 up), matching the intent of Python's round().
    expect(quantize(1.00005, 1.0)).toBe(1);
  });

  it("clamps to the int16 range", () => {
    expect(quantize(100.0, 0.0)).toBe(32767);
    expect(quantize(-100.0, 0.0)).toBe(-32768);
  });
});

describe("douglasPeucker", () => {
  it("is a no-op for lines with fewer than 3 points", () => {
    const line: Line = [
      [0, 0],
      [1, 1],
    ];
    expect(douglasPeucker(line, 0.001)).toEqual(line);
    const single: Line = [[0, 0]];
    expect(douglasPeucker(single, 0.001)).toEqual(single);
  });

  it("always keeps both endpoints", () => {
    const line: Line = [
      [0, 0],
      [1, 0.00001],
      [2, -0.00001],
      [3, 0.00001],
      [4, 0],
    ];
    const simplified = douglasPeucker(line, 1.0); // huge tolerance
    expect(simplified[0]).toEqual(line[0]);
    expect(simplified[simplified.length - 1]).toEqual(line[line.length - 1]);
    expect(simplified.length).toBe(2);
  });

  it("drops interior points that are exactly collinear with the chord", () => {
    // Every interior point lies exactly on the segment from p0 to p4, so its
    // perpendicular distance is 0 regardless of tolerance.
    const line: Line = [
      [0, 0],
      [1, 0],
      [2, 0],
      [3, 0],
      [4, 0],
    ];
    const simplified = douglasPeucker(line, 0.01);
    expect(simplified).toEqual([
      [0, 0],
      [4, 0],
    ]);
  });

  it("keeps a point that exceeds tolerance and recurses on both halves", () => {
    const line: Line = [
      [0, 0],
      [1, 10], // big deviation, kept
      [2, 0],
      [3, 10], // big deviation, kept
      [4, 0],
    ];
    const simplified = douglasPeucker(line, 0.5);
    expect(simplified).toEqual(line);
  });
});
