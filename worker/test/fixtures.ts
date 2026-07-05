// Shared Overpass response fixtures for integration tests.
import type { OverpassResponse } from "../src/types";

/** Build a way element with the given tags and lat/lon geometry pairs. */
function way(
  tags: Record<string, string>,
  coords: Array<[number, number]>
): {
  type: string;
  tags: Record<string, string>;
  geometry: Array<{ lat: number; lon: number }>;
} {
  return {
    type: "way",
    tags,
    geometry: coords.map(([lat, lon]) => ({ lat, lon })),
  };
}

// A small grid of motorway segments near a fixed center, split into two ways
// that share an endpoint (so stitching has something real to do).
export const HIGHWAY_RESPONSE: OverpassResponse = {
  elements: [
    way({ highway: "motorway" }, [
      [40.0, -105.0],
      [40.01, -105.0],
    ]),
    way({ highway: "motorway" }, [
      [40.01, -105.0],
      [40.02, -105.0],
    ]),
  ],
};

export const WATER_RESPONSE: OverpassResponse = {
  elements: [
    way({ waterway: "river" }, [
      [40.0, -105.05],
      [40.05, -105.05],
      [40.1, -105.05],
    ]),
    way({ natural: "water" }, [
      [40.02, -105.02],
      [40.03, -105.02],
      [40.03, -105.03],
      [40.02, -105.03],
      [40.02, -105.02],
    ]),
  ],
};

export const BOUNDARY_RESPONSE: OverpassResponse = {
  elements: [
    way({ boundary: "administrative", admin_level: "6" }, [
      [39.9, -105.1],
      [40.1, -105.1],
      [40.1, -104.9],
      [39.9, -104.9],
      [39.9, -105.1],
    ]),
  ],
};

export const TOWN_RESPONSE: OverpassResponse = {
  elements: [
    { type: "node", lat: 40.005, lon: -105.0, tags: { name: "Testville" } },
    { type: "node", lat: 40.015, lon: -105.0, tags: { name: "North Fork" } },
  ],
};

export const EMPTY_RESPONSE: OverpassResponse = { elements: [] };

/** Build a mock `fetch` that routes Overpass queries to canned responses by kind. */
export function makeMockFetch(responses: {
  highway?: OverpassResponse;
  water?: OverpassResponse;
  boundary?: OverpassResponse;
  town?: OverpassResponse;
}): typeof fetch {
  return (async (_url: string, init?: RequestInit) => {
    const body = String(init?.body ?? "");
    let kind: keyof typeof responses | null = null;
    if (body.includes("highway")) kind = "highway";
    else if (body.includes("waterway") || body.includes("natural")) kind = "water";
    else if (body.includes("boundary")) kind = "boundary";
    else if (body.includes("place")) kind = "town";

    const payload = kind ? responses[kind] : undefined;
    if (!payload) {
      return new Response("not found", { status: 404 });
    }
    return new Response(JSON.stringify(payload), {
      status: 200,
      headers: { "Content-Type": "application/json" },
    });
  }) as typeof fetch;
}
