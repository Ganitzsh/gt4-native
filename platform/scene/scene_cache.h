#pragma once
// The scene cache (docs/modern/ARCHITECTURE.md, ticket B6, design a92c4a2): each tagged instance's mesh (the mesh
// cache, B5) and its materials (the material table), given beside its view's package (FrameList::ViewOnList::ids and
// ::materials, CONTRACTS.md a.8). The front end calls it at each view's ViewEnd, on its thread, while the scene split is
// on; it only reads what it is given (no GS front-end types here: the front end hands over each item's batch and draw
// state as ItemSource).
#include "material_table.h"
#include "mesh_cache.h"
#include "scene_frame.h"
#include "texture_summary.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace gt4::scene {

// One item of an instance (one of its tagged jobs): its batch where it lies, and what its draw's pixels depend on.
struct ItemSource {
    BatchRef batch;
    uint32_t prim = 0;                       // the draw's PRIM (DrawState::prim)
    const gfx::Context* context = nullptr;   // its context: blend, tests, texture function, sampler
    uint8_t texAlpha0 = 0, texAlpha1 = 0, texBlackTransparent = 0, perPixelBlend = 0;
    uint64_t texture = 0;                    // its texture's identity (GsFrontend::textureIdentity); 0 untextured
    uint32_t palette = 0;                    // its texture's palette version where the identity does not hold it
};

// The calling thread's CPU time so far, in microseconds (B6's PERF line, PLAN 066b528: the front-end thread's cost without
// the preemption a wall clock counts). Windows counts cycles (QueryThreadCycleTime), converted at the rate measured once
// against the steady clock (a 20 ms spin at the first call); Linux reads CLOCK_THREAD_CPUTIME_ID.
double threadCpuMicroseconds();

// The parts of an item's key (CONTRACTS.md a.8 MaterialKey); exposed for the tests.
uint32_t materialState(const ItemSource& item);   // blend, tests, fog, texture function, filter, wrap
MaterialKey materialKey(const ItemSource& item);

class SceneCache {
public:
    explicit SceneCache(unsigned meshWorkers = 0);

    // One instance: its mesh (0: skinned, a billboard run, a key-frame blend, a light map or the road reflection, or still
    // decoding) and its materials appended to `materials` (one per item, in the mesh's item order), counted into
    // `counters` (meshesNew, meshesPending, materialsNew, meshesSkinned, meshesPerFrame, meshesOutOfBinding).
    InstanceIds assign(const InstanceData& instance, const ItemSource* items, size_t count, std::vector<MaterialId>& materials,
                       SceneCounters& counters);
    // The end of a scene frame: both caches drop what went unused.
    void endFrame();
    // Material classes (F2b's glow, ...): called for an item whose key is new, its bits kept with the id
    // (MaterialTable::classes). None by default.
    void setClassifier(std::function<uint32_t(const ItemSource&)> classify) { classifier = std::move(classify); }

    // N3 (PLAN.md, lit_road.h): the lit-road verdict of an instance's items, written to out[0 .. count) one per item in
    // the mesh's item order (1: GT4's lit-road layer, lamp light the night relight replaces): the caller passes the view's
    // skip list at the instance's firstMaterial, so the flags sit at the same index as the materials. Each item is judged
    // once per (texture, MeshKey, item index): the draw state and the texture first (per material, remembered), then
    // the geometry through the instance's objectToWorld (decodeBatch from the batch where it lies) for the items that
    // pass. A later instance of a judged item at another rotation keeps 0 and counts in n3LaterFail (a tilted copy
    // must not inherit a flat verdict). `summaries` gives a texture identity's summary (GsFrontend::textureSummary).
    void litRoad(const InstanceData& instance, const ItemSource* items, size_t count, const MaterialId* materials,
                 const std::function<bool(uint64_t, TextureSummary&)>& summaries, uint8_t* out, SceneCounters& counters);
    // The items whose material passed the draw-state and texture tests, without judging anything new: n3SkipGatedOff's
    // count, for a view where the skip is off (by day, another view, the setting off).
    size_t litRoadCandidates(const MaterialId* materials, size_t count) const;

    MeshCache& meshes() { return meshCache; }
    MaterialTable& materials() { return materialTable; }
    // The render thread's read-only view (FrameList::sceneCache): MeshCache::mesh and MaterialTable::classes / key (and
    // both caches' stats()) take their locks; SceneCache::stats() below reads unlocked front-end state: the front end's
    // only.
    const MeshCache& meshes() const { return meshCache; }
    const MaterialTable& materials() const { return materialTable; }

    // The acceptance's check (PLAN B6): the same object (InstanceData::object) keeps its mesh and material ids from one
    // sighting to the next in the same view. Counted for instances with a mesh: same ids, another mesh, other materials.
    struct Stats {
        uint64_t objectsSame = 0, objectsMeshChanged = 0, objectsMaterialsChanged = 0;
        size_t objects = 0;   // distinct (object, view) pairs seen with a mesh
    };
    Stats stats() const { Stats s = objectCounts; s.objects = seen.size(); return s; }
    void setTrackObjects(bool on) { trackObjects = on; }   // the check above (off by default: it costs a hash a sighting)

private:
    struct Seen {
        MeshId mesh = 0;
        uint64_t materials = 0;   // the hash of its material ids
    };
    std::unordered_map<uint64_t, Seen> seen;
    std::function<uint32_t(const ItemSource&)> classifier;
    bool trackObjects = false;
    Stats objectCounts;
    MeshCache meshCache;
    MaterialTable materialTable;
    std::vector<BatchRef> refs;   // (assign's, kept)

    // N3's verdicts (litRoad): per material, its draw state and texture (1 fails, 2 passes); per (texture, mesh,
    // item), the item's verdict with the rotation it was judged at and the frame it was last seen. The key is what the
    // verdict reads and outlives the caches: the texture's identity and the mesh's content key, not the MaterialId,
    // which the material table gives anew to a material unseen for 300 frames (with the MaterialId and 600 frames,
    // lap_ssr5f_boot judged about 13 items a frame again while racing, every section seen at the start being new by
    // then). Dropped after kLitRoadFrames frames unseen: a lap and the next hit; another course's go.
    // The texture's identity is the front end's (GsFrontend::textureIdentity), a content identity: a set's or an
    // upload's GS layout fields mixed with the hash of its texels, so other content at the same place is another key.
    // A streamed texture keeps its first content's identity, as its summary does (the verdict reads that summary
    // either way). A render target's or a reinterpretation's identity is by target, but has no summary: its material
    // fails the per-material test, so it never reaches this map.
    struct LitRoadKey {
        uint64_t texture = 0;
        MeshKey mesh{};
        uint32_t item = 0;
        bool operator==(const LitRoadKey& o) const { return texture == o.texture && mesh == o.mesh && item == o.item; }
    };
    struct LitRoadKeyHash {
        size_t operator()(const LitRoadKey& k) const {
            return MeshKeyHash{}(k.mesh) ^ (size_t(k.texture) * 0x9E3779B97F4A7C15ull) ^ (size_t(k.item) << 1);
        }
    };
    struct LitRoadEntry {
        uint8_t skip = 0;
        float rotation[3][3] = {};
        uint64_t lastFrame = 0;
    };
    // Items whose geometry alone fails the verdict (litRoadGeometryFails), by (mesh, item), with the objectToWorld they
    // were measured through: the same item at the same place fails with any texture, so its keep is entered in
    // litRoadVerdicts under the new texture without a measure. Some of the layer's textures change identity as the race goes (lap_ssr5f_boot: road markings
    // 0.19 m wide, 189 meshes of 95 course objects under 6,673 texture identities: 216,469 judgements for 437
    // distinct measures, about 16 a frame for the whole race), and each new identity is a new key in litRoadVerdicts.
    // 'The same place' is within kLitRoadSameLinear on the matrix's 3 x 3 (the verdicts' own rotation bound) and
    // kLitRoadSameMetres on its translation: objectToWorld comes through the camera's inverse, so a still object's
    // differs in its last bits from frame to frame (a 0 as -0). Dropped as the verdicts are.
    struct LitRoadGeometryKey {
        MeshKey mesh{};
        uint32_t item = 0;
        bool operator==(const LitRoadGeometryKey& o) const { return mesh == o.mesh && item == o.item; }
    };
    struct LitRoadGeometryKeyHash {
        size_t operator()(const LitRoadGeometryKey& k) const { return MeshKeyHash{}(k.mesh) ^ (size_t(k.item) << 1); }
    };
    static constexpr float kLitRoadSameLinear = 1e-3f, kLitRoadSameMetres = 0.01f;
    struct LitRoadGeometryEntry {
        float objectToWorld[4][4] = {};
        uint64_t lastFrame = 0;
    };
    static constexpr uint64_t kLitRoadFrames = 36000;      // (10 minutes of scene frames)
    static constexpr uint64_t kLitRoadPruneFrames = 600;   // how often endFrame looks for verdicts unseen that long
    // (MaterialIds are never reused: emptied past kLitRoadMaterialsMax, as F2b's classesById, and refilled cheaply)
    static constexpr size_t kLitRoadMaterialsMax = 100000;
    std::unordered_map<MaterialId, uint8_t> litRoadMaterials;
    std::unordered_map<LitRoadKey, LitRoadEntry, LitRoadKeyHash> litRoadVerdicts;
    std::unordered_map<LitRoadGeometryKey, LitRoadGeometryEntry, LitRoadGeometryKeyHash> litRoadFailedGeometry;
    std::vector<uint8_t> litRoadCandidate;   // (litRoad's, kept)
    uint64_t frames = 0;                     // endFrame's count
};

}  // namespace gt4::scene
