#pragma once
// B7's parity path and B8's hybrid (docs/modern/PLAN.md B7, B8; render.scene = parity, hybrid): the GS path with one
// change. Each tagged model run (one job, one item of an instance) of a view the front end substituted
// (ViewOnList::ids) is drawn from the mesh cache by modern_parity.vert instead of from gsn_models.comp's output, at its
// own place in the list's command order. This is the planning half, on the CPU: which items have their mesh on the GPU,
// and for each Draw the vertex ranges they replace (a Draw can merge several jobs, of several instances: gsn_scene.cpp
// draws the gaps from gsn_models.comp's output). What is not drawn here falls back to the GS path, counted by reason.
//
// The meshes live in device-local blocks (B8 step 1): a mesh's raw vertices and its triangles in one range of a block,
// written into a staging stream when the mesh is first drawn and copied by gsn_scene.cpp before the list's draws;
// retired when unused for a while or over the budget, and their ranges freed once no frame in flight can read them.
#include "gs_frontend.h"
#include "mesh_cache.h"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace gt4::render {

// Ranges of fixed-size blocks: first fit over each block's free list, blocks added on demand up to a count. Pure
// bookkeeping (the buffers are the caller's): a block is an index, a range an offset in it.
class BlockRanges {
public:
    BlockRanges(uint64_t blockBytes, uint32_t maxBlocks) : blockBytes(blockBytes), maxBlocks(maxBlocks) {}
    struct Range {
        uint32_t block = 0;
        uint64_t offset = 0, bytes = 0;
    };
    // False when no block has room and no block can be added (or the range is larger than a block).
    bool allocate(uint64_t bytes, uint64_t alignment, Range& out);
    void release(const Range& range);
    uint32_t blocks() const { return static_cast<uint32_t>(freeLists.size()); }
    uint64_t used() const { return usedBytes; }
    uint64_t blockSize() const { return blockBytes; }

private:
    uint64_t blockBytes;
    uint32_t maxBlocks;
    uint64_t usedBytes = 0;
    std::vector<std::map<uint64_t, uint64_t>> freeLists;   // per block: offset -> size
};

// One item drawn by the substitution.
struct ParitySpan {
    uint32_t draw = 0;                 // the Draw (FrameList::draws) holding the job's vertices
    uint32_t first = 0, count = 0;     // the job's vertices in the list's model vertices (ItemRef)
    uint32_t job = 0;                  // FrameList::models: its constants and words (modern_parity.vert reads them)
    uint32_t renderer = 0;             // ModelJob::renderer
    uint32_t block = 0;                // the mesh's block
    uint64_t vertexOffset = 0;         // its raw vertices in the block (bytes: bound as the vertex buffer's offset)
    uint64_t indexOffset = 0;          // its triangles in the block (bytes: the index buffer's offset)
    uint32_t firstIndex = 0, indexCount = 0;   // the item's triangles among the mesh's
    // B8's side channels: where the item's instance and material are (FrameList::views[view].package->instances[instance],
    // its MaterialId)
    uint32_t view = 0, instance = 0;
    scene::MaterialId material = 0;
};

// A copy the caller records before the list's draws: `bytes` from the staging stream at `from` into `block` at `to`.
struct ParityCopy {
    uint64_t from = 0, to = 0, bytes = 0;
    uint32_t block = 0;
};

// Why an item was not drawn here (PLAN B7's viewFallbacks reasons), and how many were.
struct ParityStats {
    uint64_t items = 0;            // tagged items of the lists' substituted views
    uint64_t drawn = 0;            // drawn from the mesh cache
    uint64_t viewsNotSubstituted = 0;   // views cut or not valid (no ids: B6), wholly on the GS path (views, not items)
    uint64_t viewsOfRoleKept = 0;       // views of a role never substituted (the HUD's, menus', an env face's copy)
    uint64_t itemsOutOfBinding = 0;     // items of light-map and road-reflection instances (CONTRACTS a.5.7): GS path
    uint64_t viewsForReadSync = 0;      // B8 step 4: views on the GS path because their list was cut for a game read
    uint64_t viewsForReadAsync = 0;     // ... because an asynchronous read names their target
    uint64_t meshNone = 0;         // mesh 0: skinned, a billboard run, a key-frame blend, or still decoding
    uint64_t meshGone = 0;         // evicted before the render thread read it
    uint64_t itemsDiffer = 0;      // the mesh's items do not match the instance's runs (should not happen)
    uint64_t overBudget = 0;       // no room in the blocks even after retiring (the GS path drew them)
    uint64_t meshesPlaced = 0;     // meshes uploaded
    uint64_t meshesRetired = 0;    // meshes dropped from the GPU (unused, or the budget)
    uint64_t uploadBytes = 0;
};

class ParityView {
public:
    // blockBytes and maxBlocks: the GPU budget (blockBytes x maxBlocks); unusedFrames: a mesh not drawn for that many
    // frames is retired; framesInFlight: a retired range is reused only after that many more frames.
    // rotate: no last provoking vertex on this device (VK_EXT_provoking_vertex), so each triangle is rotated (v, a, b) to
    // put the strip's last vertex first (its vertex order no longer the GS path's: Z may differ).
    ParityView(uint64_t blockBytes = 32ull << 20, uint32_t maxBlocks = 8, uint32_t unusedFrames = 300, uint32_t framesInFlight = 3);
    void setRotate(bool on) { rotate = on; }
    // The list's spans, sorted by (draw, first), and the uploads its new meshes need (appended to `staging` and
    // `copies`, which the caller empties when it has recorded them). The triangles are written in the GS path's vertex
    // order (v - 2, v - 1, v), the mesh's turned ones (Mesh::turned) turned back: the rasterizer interpolates Z from the
    // vertices in their order, and the turned odd triangles gave other Z at GT4's coplanar layers.
    void plan(const gsn::FrameList& list, std::vector<ParitySpan>& spans, std::vector<uint8_t>& staging, std::vector<ParityCopy>& copies);
    // B8 step 4, the read switch (PLAN B8; RISKS R9 as a per-reader table): a game read of a view's picture must see the
    // PS2's, so a view whose list holds one draws on the GS path (its items left out). An asynchronous read
    // (FrameList::readbacks; its list is cut too) leaves the views of the target it names; a list cut with no readback
    // was cut for a synchronous read (its target not known here) and leaves all its views. Every reader is GS-only until R9 checks it; off (false), the switch lets
    // the reads see the substituted picture (R9's checks).
    void setReadSwitch(bool on) { readSwitch = on; }
    // A frame is done (after its lists): meshes unused for unusedFrames are retired, and ranges retired framesInFlight
    // frames ago are freed.
    void endFrame();
    uint32_t blocks() const { return ranges.blocks(); }
    uint64_t blockSize() const { return ranges.blockSize(); }
    uint64_t bytesUsed() const { return ranges.used(); }
    const ParityStats& stats() const { return counters; }

private:
    struct Placed {
        BlockRanges::Range range;
        uint64_t indexOffset = 0;      // in the block
        std::vector<scene::MeshItem> items;
        uint64_t lastUsed = 0;
    };
    const Placed* place(const scene::Mesh& mesh, scene::MeshId id, std::vector<uint8_t>& staging, std::vector<ParityCopy>& copies);
    bool makeRoom(uint64_t bytes);     // retires the least recently used meshes not drawn in the frames in flight
    BlockRanges ranges;
    uint32_t unusedFrames, framesInFlight;
    bool rotate = false, readSwitch = true;
    uint64_t frame = 0;
    std::unordered_map<scene::MeshId, Placed> placed;
    std::vector<std::pair<uint64_t, BlockRanges::Range>> retired;   // (the frame it was retired at, its range)
    ParityStats counters;
};

}  // namespace gt4::render
