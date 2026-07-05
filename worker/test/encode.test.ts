import { describe, expect, it } from "vitest";
import { encodeMapBlob, HEADER_BYTES } from "../src/encode";

describe("encodeMapBlob", () => {
  it("produces the exact byte layout for a small fixture", () => {
    const blob = encodeMapBlob({
      centerLat: 39.7392,
      centerLon: -104.9903,
      verts: [
        { dlat: 1, dlon: -1 },
        { dlat: 100, dlon: -100 },
      ],
      spans: [{ start: 0, len: 2, layer: 0 }],
      towns: [{ lat: 39.7395, lon: -104.99, label: "DEN" }],
    });

    // Total length: header(24) + 2 verts * 4 + 1 span * 5 + 1 town * 9
    expect(blob.length).toBe(HEADER_BYTES + 2 * 4 + 1 * 5 + 1 * 9);

    const view = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);

    // Magic "PRMB"
    expect(String.fromCharCode(blob[0], blob[1], blob[2], blob[3])).toBe(
      "PRMB"
    );
    // Version
    expect(view.getUint8(4)).toBe(1);
    // Reserved bytes 5-7 zero
    expect(view.getUint8(5)).toBe(0);
    expect(view.getUint8(6)).toBe(0);
    expect(view.getUint8(7)).toBe(0);
    // Center lat/lon (float32, so compare with tolerance)
    expect(view.getFloat32(8, true)).toBeCloseTo(39.7392, 4);
    expect(view.getFloat32(12, true)).toBeCloseTo(-104.9903, 4);
    // Counts
    expect(view.getUint16(16, true)).toBe(2); // vertCount
    expect(view.getUint16(18, true)).toBe(1); // spanCount
    expect(view.getUint16(20, true)).toBe(1); // townCount
    expect(view.getUint16(22, true)).toBe(0); // reserved

    // Verts start at offset 24
    expect(view.getInt16(24, true)).toBe(1); // dlat
    expect(view.getInt16(26, true)).toBe(-1); // dlon
    expect(view.getInt16(28, true)).toBe(100); // dlat
    expect(view.getInt16(30, true)).toBe(-100); // dlon

    // Spans start at offset 24 + 2*4 = 32
    expect(view.getUint16(32, true)).toBe(0); // start
    expect(view.getUint16(34, true)).toBe(2); // len
    expect(view.getUint8(36)).toBe(0); // layer

    // Towns start at offset 32 + 5 = 37
    const townOff = 37;
    // dlat/dlon quantized from (39.7395, -104.99) relative to center
    // (39.7392, -104.9903): dlat = round((39.7395-39.7392)/1e-4) = 3,
    // dlon = round((-104.99-(-104.9903))/1e-4) = round(0.0003/1e-4) = 3.
    expect(view.getInt16(townOff, true)).toBe(3);
    expect(view.getInt16(townOff + 2, true)).toBe(3);
    // Label "DEN" + zero padding to 5 bytes.
    expect(view.getUint8(townOff + 4)).toBe("D".charCodeAt(0));
    expect(view.getUint8(townOff + 5)).toBe("E".charCodeAt(0));
    expect(view.getUint8(townOff + 6)).toBe("N".charCodeAt(0));
    expect(view.getUint8(townOff + 7)).toBe(0);
    expect(view.getUint8(townOff + 8)).toBe(0);
  });

  it("produces an empty-body blob (header only) for no data", () => {
    const blob = encodeMapBlob({
      centerLat: 0,
      centerLon: 0,
      verts: [],
      spans: [],
      towns: [],
    });
    expect(blob.length).toBe(HEADER_BYTES);
    const view = new DataView(blob.buffer);
    expect(view.getUint16(16, true)).toBe(0);
    expect(view.getUint16(18, true)).toBe(0);
    expect(view.getUint16(20, true)).toBe(0);
  });

  it("truncates labels longer than 4 usable chars", () => {
    const blob = encodeMapBlob({
      centerLat: 0,
      centerLon: 0,
      verts: [],
      spans: [],
      towns: [{ lat: 0, lon: 0, label: "TOOLONG" }],
    });
    const view = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
    const labelOff = HEADER_BYTES + 4;
    const chars = [0, 1, 2, 3, 4].map((i) => view.getUint8(labelOff + i));
    expect(String.fromCharCode(...chars.slice(0, 4))).toBe("TOOL");
    expect(chars[4]).toBe(0); // final byte always zero-terminated
  });

  it("UTF-8 encodes non-ASCII labels instead of truncating to one byte per UTF-16 code unit", () => {
    const blob = encodeMapBlob({
      centerLat: 0,
      centerLon: 0,
      verts: [],
      spans: [],
      towns: [{ lat: 0, lon: 0, label: "CAÑ" }],
    });
    const view = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
    const labelOff = HEADER_BYTES + 4;
    // "CAÑ" is 4 bytes in UTF-8 (C, A, then Ñ = 0xC3 0x91) -- fits in 4
    // usable bytes exactly, so nothing here should be lost or mis-truncated.
    const expected = new TextEncoder().encode("CAÑ");
    expect(expected.length).toBe(4);
    for (let i = 0; i < 4; i++) {
      expect(view.getUint8(labelOff + i)).toBe(expected[i]);
    }
    expect(view.getUint8(labelOff + 4)).toBe(0); // zero-terminated
  });
});
