#pragma once
// B7's parity path (docs/modern/PLAN.md B7; render.scene = parity): the GS path with one change. Each tagged model run
// (one job, one item of an instance) of a view the front end substituted (ViewOnList::ids) is drawn from the mesh cache by
// modern_parity.vert instead of from gsn_models.comp's output, at its own place in the list's command order. This is the
// planning half, on the CPU: which items have their mesh in the arena (the meshes' raw vertices and triangles, copied once
// into host-visible memory the GPU reads), and for each Draw the vertex ranges they replace (a Draw can merge several
// jobs, of several instances: gsn_scene.cpp draws the gaps from gsn_models.comp's output). What is not drawn here falls
// back to the GS path, counted by reason.
#include "gs_frontend.h"
#include "mesh_cache.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gt4::render {

// One item drawn by the parity path.
struct ParitySpan {
    uint32_t draw = 0;                 // the Draw (FrameList::draws) holding the job's vertices
    uint32_t first = 0, count = 0;     // the job's vertices in the list's model vertices (ItemRef)
    uint32_t job = 0;                  // FrameList::models: its constants and words (modern_parity.vert reads them)
    uint32_t renderer = 0;             // ModelJob::renderer (the cull: gsn_scene.cpp)
    uint32_t vertexOffset = 0;         // the mesh's first vertex in the arena
    uint32_t firstIndex = 0, indexCount = 0;   // the item's triangles in the arena
};

// Why an item was not drawn here (PLAN B7's viewFallbacks reasons), and how many were.
struct ParityStats {
    uint64_t items = 0;            // tagged items of the lists' substituted views
    uint64_t drawn = 0;            // drawn from the mesh cache
    uint64_t viewsNotSubstituted = 0;   // views cut or not valid (no ids: B6), wholly on the GS path (views, not items)
    uint64_t meshNone = 0;         // mesh 0: skinned, a key-frame blend, or still decoding when the view was assigned
    uint64_t meshGone = 0;         // evicted before the render thread read it
    uint64_t itemsDiffer = 0;      // the mesh's items do not match the instance's runs (should not happen)
    uint64_t arenaResets = 0;      // the arena filled and was emptied (the GPU waited for)
    uint64_t meshesPlaced = 0;     // meshes copied into the arena
};

class ParityView {
public:
    // The arena (host-visible, mapped): `vertices` holds vertexCapacity raw vertices, `indices` indexCapacity indices.
    // The triangles are written in the GS path's vertex order (v - 2, v - 1, v), the mesh's turned ones (Mesh::turned)
    // turned back: the rasterizer interpolates Z from the vertices in their order, and the turned odd triangles gave
    // other Z at GT4's coplanar layers. (Which triangles are drawn, modern_parity.vert decides as gsn_models.comp.)
    // rotate: no last provoking vertex on this device (VK_EXT_provoking_vertex), so each triangle is rotated (v, a, b) to
    // put the strip's last vertex first (its vertex order no longer the GS path's: Z may differ).
    void setArena(scene::MeshRawVertex* vertices, uint32_t vertexCapacity, uint32_t* indices, uint32_t indexCapacity, bool rotate);
    // The list's spans, sorted by (draw, first). False when the arena filled: the caller waits for the GPU, calls reset()
    // and plans again (the list's items counted once).
    bool plan(const gsn::FrameList& list, std::vector<ParitySpan>& spans);
    void reset();   // the arena emptied (nothing placed): only with the GPU idle
    const ParityStats& stats() const { return counters; }

private:
    struct Placed {
        uint32_t vertexOffset = 0, firstIndex = 0;
        std::vector<scene::MeshItem> items;
    };
    const Placed* place(const scene::Mesh& mesh, scene::MeshId id);
    scene::MeshRawVertex* vertices = nullptr;
    uint32_t* indices = nullptr;
    uint32_t vertexCapacity = 0, indexCapacity = 0, vertexUsed = 0, indexUsed = 0;
    bool rotate = false, full = false;
    std::unordered_map<scene::MeshId, Placed> placed;
    ParityStats counters;
};

}  // namespace gt4::render
