#pragma once
// N3 (docs/modern/PLAN.md; docs/modern/spikes/N3-lit-road-rule.md): GT4's lit-road layer, the street lamps' light
// baked into the course as textured strips drawn over the road with a draw state of their own. The same state also
// draws road markings, the start/finish checkers, logos and a fence, so the rule (v4, the architect, 2026-11-09) judges
// each item at first sight from what it is: an item is lamp light when it has the layer's state; a mean vertex colour
// of luminance over 16 and alpha at least 100; a texture centre/rim under 3 (not a glow); at least 0.9 of its area with
// |n.y| >= 0.9 in the world; a footprint width of at least 0.9 m (2 x area / perimeter of its up-facing pieces); and
// at least 0.03 m per texel. Measured on Clubman Stage Route 5 and Special Stage Route 5 in both directions: markings
// reach 0.60 m wide, the checkers 0.017 m per texel, the pools 0.039 and up.
#include "mesh_cache.h"
#include "scene_cache.h"
#include "texture_summary.h"

namespace gt4::scene {

// The layer's draw state: textured and alpha-blended as (Cs - Cd) As + Cd, no alpha test, z GEQUAL without z write,
// and lighting body 0 (no lighting chain).
bool litRoadState(const ItemSource& item);

struct LitRoadMeasure {
    float vertexLuminance = 0.0f;   // the mean vertex colour's Rec.709 luminance (0..255)
    float vertexAlpha = 0.0f;       // its mean alpha (0x80 = 1.0)
    float centreToRim = -1.0f;      // the texture summary's (-1: no summary)
    float upShare = 0.0f;           // the share of the item's world area with |n.y| >= 0.9
    float width = 0.0f;             // metres: 2 x area / perimeter of each up-facing piece, area-weighted
    float texel = 0.0f;             // metres per texel: max(|dP/ds| / width, |dP/dt| / height), area-weighted
};

// Measures an item: its decoded batch through objectToWorld (row vectors: [p 1] x M, metres), with its texture's
// summary (nullptr or an unknown one: centreToRim -1 and texel 0).
LitRoadMeasure measureLitRoad(const DecodedBatch& batch, const float (&objectToWorld)[4][4], const TextureSummary* texture);

// v4's thresholds, for an item whose state is the layer's.
bool litRoadVerdict(const LitRoadMeasure& m);
// Whether the verdict fails on the measures that don't read the texture (the vertex colour, the up-facing share, the
// width): then it fails with any texture, for the same batch through the same objectToWorld.
bool litRoadGeometryFails(const LitRoadMeasure& m);

}  // namespace gt4::scene
