#pragma once
// The scene cache (docs/modern/ARCHITECTURE.md, ticket B6, design a92c4a2): each tagged instance's mesh (the mesh
// cache, B5) and its materials (the material table), given beside its view's package (FrameList::ViewOnList::ids and
// ::materials, CONTRACTS.md a.8). The front end calls it at each view's ViewEnd, on its thread, while the scene split is
// on; it only reads what it is given (no GS front-end types here: the front end hands over each item's batch and draw
// state as ItemSource).
#include "material_table.h"
#include "mesh_cache.h"
#include "scene_frame.h"

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

    // One instance: its mesh (0: skinned, an item with a key-frame blend, or still decoding) and its materials appended
    // to `materials` (one per item, in the mesh's item order), counted into `counters` (meshesNew, meshesPending,
    // materialsNew, meshesSkinned, meshesPerFrame).
    InstanceIds assign(const InstanceData& instance, const ItemSource* items, size_t count, std::vector<MaterialId>& materials,
                       SceneCounters& counters);
    // The end of a scene frame: both caches drop what went unused.
    void endFrame();
    // Material classes (F2b's glow, ...): called for an item whose key is new, its bits kept with the id
    // (MaterialTable::classes). None by default.
    void setClassifier(std::function<uint32_t(const ItemSource&)> classify) { classifier = std::move(classify); }

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
};

}  // namespace gt4::scene
