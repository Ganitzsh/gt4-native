#pragma once
// The mesh cache (docs/modern/ARCHITECTURE.md 3.1, ticket B5): GT4's model batches (gfx::ModelJob and the
// batch's 1024 words, as the 3D renderers get them) decoded once into object-space meshes, the form the modern
// views draw from. A batch decodes as the CPU renderers' lighting chain does (platform/gs/renderers3d.cpp:
// positions as integers or floats, texture coordinates in 4.12, normals as bytes over 127, the key-frame blend);
// its strips become a triangle list (the strips' first two vertices start no triangle, as their ADC bits say).
// The items of one object are welded into one mesh: until the scene records' instances land (ticket A2), an
// object is a run of consecutive jobs with the same transform.
//
// Lighting, culling and the projection are per frame and stay out: a mesh keeps what they read (normals or the
// given colours, the renderer's culling side, the chain's body), not their results.
#include "gt4/gfx.h"
#include "scene_frame.h"   // MeshKey, MeshKeyHash, MeshId (docs/modern/CONTRACTS.md a.8)

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gt4::scene {

// One vertex in object space.
struct MeshVertex {
    float position[3] = {};
    float st[2] = {};          // texture coordinates as the GS takes them before the Q divide (S, T)
    float normal[3] = {};      // lit models (zero when the batch has none)
    uint32_t colour = 0;       // the given colour words as RGBA8 (unlit and prelit models), 0x80 = 1.0 alpha
};

// How a batch was drawn; the per-frame passes need it, the geometry does not change with it.
struct MeshItem {
    uint32_t firstIndex = 0, indexCount = 0;   // in Mesh::indices
    uint32_t renderer = 0;     // ModelJob::renderer: 0 strips, 1/2 culled back/front, 3..5 the clipping ones
    uint32_t chain = 0;        // ModelJob::chain (bit 0 lit, its routine and body)
    uint32_t dot = 0;
    uint32_t prim = 0;
    bool normals = false, texture = false, animated = false;   // animated: a key-frame blend (its weight is per frame)
};

struct Mesh {
    MeshKey key{};
    std::vector<MeshVertex> vertices;    // welded: equal vertices of the items share one entry
    std::vector<uint32_t> indices;       // triangles, each in one winding (the strips' alternation undone)
    std::vector<MeshItem> items;
    uint32_t sourceVertices = 0;         // before welding (the batches' vertices)
    size_t bytes() const { return vertices.size() * sizeof(MeshVertex) + indices.size() * 4 + items.size() * sizeof(MeshItem); }
};

// One batch as the 3D renderers got it.
struct BatchCopy {
    gfx::ModelJob job{};
    uint32_t words[gfx::kModelWords] = {};
};

// One batch where it lies (a FrameList's job and its words in modelData): no copy (MeshCache::request's fast path).
struct BatchRef {
    const gfx::ModelJob* job = nullptr;
    const uint32_t* words = nullptr;   // gfx::kModelWords of them
};

// What a request found (B6's counters): the mesh decoded, asked for before and still decoding, asked for the first
// time (queued now), or not cached (an item with a key-frame blend: its geometry is this frame's only).
enum class MeshRequest : uint8_t { Ready, Pending, Queued, PerFrame };

// The decode of one batch (its vertices in batch order, and its triangles over them); exposed for the tests.
struct DecodedBatch {
    std::vector<MeshVertex> vertices;    // the first job.count of the batch (the strips' vertices)
    std::vector<uint32_t> triangles;     // indices into vertices, three per triangle
    std::vector<uint8_t> starts;         // 1 for a vertex that starts no triangle (the first two of each strip)
    bool normals = false, texture = false, animated = false;
};
DecodedBatch decodeBatch(const gfx::ModelJob& job, const uint32_t* words);

// The items, decoded and welded into one mesh.
Mesh buildMesh(const std::vector<BatchCopy>& items);

// A key for the items' contents (what a later frame sends again for the same object).
MeshKey meshKey(const std::vector<BatchCopy>& items);
MeshKey meshKey(const BatchRef* items, size_t count);

// Groups a frame's jobs into objects until instances are known: consecutive jobs with the same matrix.
std::vector<std::pair<uint32_t, uint32_t>> groupByMatrix(const gfx::ModelJob* jobs, size_t count);   // (first, count) runs

class MeshCache {
public:
    explicit MeshCache(unsigned workers = 0);   // 0: a quarter of the cores, 1..4
    ~MeshCache();
    MeshCache(const MeshCache&) = delete;
    MeshCache& operator=(const MeshCache&) = delete;

    // The mesh for these items: its id once decoded (0 while it is pending: asked for on a worker the first time).
    // Per-frame geometry is not cached: items with a key-frame blend (their weight changes every frame) give 0
    // and count in Stats::perFrame; the caller decodes them itself (decodeBatch) or falls back.
    MeshId request(const std::vector<BatchCopy>& items);
    // The same from batches where they lie: the key is hashed in place, the items copied only when queued (a miss).
    MeshId request(const BatchRef* items, size_t count, MeshRequest* outcome = nullptr);
    MeshId find(const MeshKey& key) const;       // 0: not known, not decoded yet, or evicted
    // Decoded meshes only (null: unknown, pending or evicted). Shared: a reader on another thread (the render thread, B7,
    // F2b) keeps it alive across an eviction by endFrame() on the front-end thread.
    std::shared_ptr<const Mesh> mesh(MeshId id) const;
    void wait();                                 // until every requested decode is done (tests)

    // The end of a frame: meshes not asked for in the last `unusedFrames` frames are dropped, then the least
    // recently used ones until the cache holds at most `budgetBytes` (objects drawn once, geometry the game rewrites
    // per frame: skinned Cmds, runs of jobs whose contents change with visibility, until instances are known).
    void endFrame();
    void setLimits(uint32_t unusedFrames, uint64_t budgetBytes);

    struct Stats {
        uint64_t requests = 0, hits = 0, decoded = 0, pending = 0, perFrame = 0, evicted = 0;
        uint64_t vertices = 0, sourceVertices = 0, triangles = 0, decodedBytes = 0;   // decoded so far
        uint64_t bytes = 0;                      // held now
        double decodeMilliseconds = 0.0;         // on the workers, summed
    };
    Stats stats() const;

private:
    struct Entry {
        std::atomic<bool> ready{false};
        std::shared_ptr<const Mesh> mesh;        // written by the worker before `ready`
        uint32_t lastUsed = 0;                   // the frame it was last asked for
        bool evicted = false;
    };
    uint32_t frame = 0, unusedFrames = 300;
    uint64_t budgetBytes = 512ull << 20;
    void work();
    mutable std::mutex lock;
    std::condition_variable wake, idle;
    std::unordered_map<MeshKey, MeshId, MeshKeyHash> ids;
    std::vector<std::unique_ptr<Entry>> entries;   // index = id - 1
    std::deque<std::pair<MeshId, std::vector<BatchCopy>>> queue;
    uint32_t busy = 0;
    bool stopping = false;
    Stats counters;
    std::vector<std::thread> threads;
};

}  // namespace gt4::scene
