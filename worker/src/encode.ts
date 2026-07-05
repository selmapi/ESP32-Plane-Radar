// Binary wire-format encoder. See docs/superpowers/plans/
// 2026-07-05-v3-map-service-phase1.md "Wire format" for the byte layout.
//
// Deliberately field-by-field via DataView -- never a raw struct cast --
// so the layout can never depend on this runtime's memory layout, matching
// the device-side parsing requirement.
import type { MapSpan, QuantVert, Town } from "./types";
import { quantize } from "./geo";

export const MAGIC = "PRMB";
export const VERSION = 1;

export const HEADER_BYTES = 24;
export const VERT_BYTES = 4;
export const SPAN_BYTES = 5;
export const TOWN_BYTES = 9;
const TOWN_LABEL_BYTES = 5;

export interface EncodeInput {
  centerLat: number;
  centerLon: number;
  verts: QuantVert[];
  spans: MapSpan[];
  towns: Town[];
}

const labelEncoder = new TextEncoder();

function writeLabel(view: DataView, offset: number, label: string): void {
  // char label[5]: up to 4 usable UTF-8 bytes (matching the Python
  // generator's 4-char label cap) + zero padding/terminator. UTF-8 encode
  // (not a per-UTF-16-code-unit truncation) so non-ASCII place names (e.g.
  // "Cañon") produce the same bytes the Python pipeline would embed.
  const bytes = new Uint8Array(TOWN_LABEL_BYTES);
  const encoded = labelEncoder.encode(label).slice(0, TOWN_LABEL_BYTES - 1);
  bytes.set(encoded);
  for (let i = 0; i < TOWN_LABEL_BYTES; i++) {
    view.setUint8(offset + i, bytes[i]);
  }
}

/** Encode the full binary blob per the Phase 1 wire format spec. */
export function encodeMapBlob(input: EncodeInput): Uint8Array {
  const { centerLat, centerLon, verts, spans, towns } = input;
  const total =
    HEADER_BYTES +
    verts.length * VERT_BYTES +
    spans.length * SPAN_BYTES +
    towns.length * TOWN_BYTES;
  const buf = new ArrayBuffer(total);
  const view = new DataView(buf);

  // Header
  for (let i = 0; i < 4; i++) view.setUint8(i, MAGIC.charCodeAt(i));
  view.setUint8(4, VERSION);
  view.setUint8(5, 0);
  view.setUint8(6, 0);
  view.setUint8(7, 0);
  view.setFloat32(8, centerLat, true);
  view.setFloat32(12, centerLon, true);
  view.setUint16(16, verts.length, true);
  view.setUint16(18, spans.length, true);
  view.setUint16(20, towns.length, true);
  view.setUint16(22, 0, true);

  let off = HEADER_BYTES;
  for (const v of verts) {
    view.setInt16(off, v.dlat, true);
    view.setInt16(off + 2, v.dlon, true);
    off += VERT_BYTES;
  }
  for (const s of spans) {
    view.setUint16(off, s.start, true);
    view.setUint16(off + 2, s.len, true);
    view.setUint8(off + 4, s.layer);
    off += SPAN_BYTES;
  }
  for (const t of towns) {
    view.setInt16(off, quantize(t.lat, centerLat), true);
    view.setInt16(off + 2, quantize(t.lon, centerLon), true);
    writeLabel(view, off + 4, t.label);
    off += TOWN_BYTES;
  }

  return new Uint8Array(buf);
}
