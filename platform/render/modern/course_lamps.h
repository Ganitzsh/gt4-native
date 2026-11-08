#pragma once
// The course's lamps from its meshes (PLAN.md F2b): GT4 sends no light for its street lamps (A7: they are in none of
// the game's lists), but draws each lamp head's glow as course geometry, quads with a glow material (B6's material
// table names it: a draw-state signature and the texture's content). These are the lamps found in one mesh item's
// triangles: its quads (two triangles sharing an edge), then the quads whose centres coincide merged into one lamp (a
// star-shaped glow is several crossed quads). In the mesh's object space: the caller places them with the instance's
// transform (world = p x diag(axisScale x k, 1) x objectToWorld, scene_frame.h's row vectors; course instances have
// objectToView == worldToView, but not always unit scale). Found once per mesh item and cached by the caller, keyed by
// the mesh's content (MeshKey), since an evicted mesh asked for again comes back under a new MeshId.
#include "mesh_cache.h"   // gt4::scene::Mesh (B5)

#include <cstdint>
#include <vector>

namespace gt4::render {

struct CourseLamp {
    float position[3] = {};   // the merged quads' centres averaged, object space
    // Half the largest quad's diagonal: a half size, as gfx::scene::Light's width and height are (the glow's reach
    // from its centre), not its full extent.
    float radius = 0.0f;
    uint32_t quads = 0;       // quads merged into it (a single triangle counts as one)
};

struct LampStats {
    uint32_t triangles = 0;     // the item's, degenerate ones included
    uint32_t degenerate = 0;    // two equal indices or no area (a strip's restart): left out
    uint32_t quads = 0;         // quads made, lone triangles included
    uint32_t skewQuads = 0;     // of them, pairs that are not a rectangle (a glow quad is one: a mesh of other shapes)
    uint32_t loneTriangles = 0, lamps = 0;
};

// Triangles pair into rectangles first (opposite vertices summing to the shared edge's, equal diagonals), so a row of
// quads sharing edges pairs within each quad; what is left pairs with any neighbour (counted as skew). Quads whose
// centres are within `merge` x the smaller quad's diagonal are one lamp. 0.25: crossed quads of one glow share their
// centre; two lamps on one post are a lamp-size apart.
std::vector<CourseLamp> extractLamps(const scene::Mesh& mesh, uint32_t item, float merge = 0.25f, LampStats* stats = nullptr);

}  // namespace gt4::render
