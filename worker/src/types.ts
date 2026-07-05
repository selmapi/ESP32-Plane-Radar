// Shared types for the map-service pipeline. Mirrors scripts/build_region_map.py.

/** A single OSM point: [lat, lon] in degrees. */
export type Point = [number, number];

/** A polyline: an ordered list of points. */
export type Line = Point[];

export interface OverpassGeometryPoint {
  lat: number;
  lon: number;
}

export interface OverpassElement {
  type: string;
  tags?: Record<string, string>;
  geometry?: OverpassGeometryPoint[];
  lat?: number;
  lon?: number;
}

export interface OverpassResponse {
  elements: OverpassElement[];
}

export const HIGHWAY_CLASSES = ["motorway", "trunk", "primary"] as const;
export type HighwayClass = (typeof HIGHWAY_CLASSES)[number];

/** Layer ids -- must match ui::radar::MapLayer in include/ui/region_map.h. */
export const LAYER_HIGHWAY = 0;
export const LAYER_WATER = 1;
export const LAYER_BOUNDARY = 2;
export const LAYER_TOWN = 3;

export interface Town {
  lat: number;
  lon: number;
  label: string;
}

export interface MapSpan {
  start: number;
  len: number;
  layer: number;
}

export interface QuantVert {
  dlat: number;
  dlon: number;
}

export interface EncodedLayers {
  verts: QuantVert[];
  spans: MapSpan[];
  /** Count of DP-degenerate (<2 point) lines dropped, per layer id. */
  dropped: Record<number, number>;
}
