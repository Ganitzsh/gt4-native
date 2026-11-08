#pragma once
// F2b's lamp pass (PLAN.md F2b): the course's street lamps as lights. For a view's instances with a mesh (B6), each item
// whose material has the glow class (glow_classifier.h) gives its lamps (course_lamps.h), placed with the instance's
// transform into view space and sent on as Light records (kSrcCourseGlow) beside the game's own (A7), for the gather
// (lights_gather.h, without kFromGame). The render thread's: what it learns is cached per material id (classes and
// colour never change for an id) and per (mesh key, item) (an evicted mesh asked for again comes back under a new id).
#include "course_lamps.h"
#include "gt4/gfx.h"
#include "scene_frame.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace gt4::render {

// Where the pass reads B6's caches (SceneCache: MeshCache::mesh, MaterialTable::classes, and the classifier's colour by
// the material's texture). Functions, so a test can give a synthetic frame.
struct LampSources {
    std::function<std::shared_ptr<const scene::Mesh>(scene::MeshId)> mesh;           // null: dropped or not decoded
    std::function<uint32_t(scene::MaterialId)> classes;                                // 0: none or unknown
    std::function<bool(scene::MaterialId, float colour[3])> glowColour;                // the glow texture's mean, 0..1
};

class CourseLampPass {
public:
    struct Stats {
        uint32_t instances = 0;          // the view's instances with a mesh
        uint32_t glowItems = 0;          // items with the glow class
        uint32_t lamps = 0;              // lights sent
        uint32_t meshMissing = 0;        // glow items whose mesh was not there (dropped, pending): no lamps this frame
        uint32_t extracted = 0, cacheHits = 0;   // (mesh key, item) extractions this frame, and reuses
        uint32_t largestItemQuads = 0, skewQuads = 0, degenerate = 0;   // of this frame's extractions
    };
    // Appends the lamps of view `view` of `frame` to `out`, as Light records in that view's space.
    void lights(const scene::SceneFrame& frame, uint16_t view, const LampSources& sources, std::vector<gfx::scene::Light>& out,
                Stats* stats = nullptr);

private:
    struct Material {
        uint32_t classes = 0;
        float colour[3] = {};
    };
    struct ItemKey {
        scene::MeshKey mesh{};
        uint32_t item = 0;
    };
    struct ItemKeyHash {
        size_t operator()(const ItemKey& k) const { return scene::MeshKeyHash{}(k.mesh) * 31u + k.item; }
    };
    struct ItemKeyEqual {
        bool operator()(const ItemKey& a, const ItemKey& b) const { return a.item == b.item && a.mesh == b.mesh; }
    };
    std::unordered_map<scene::MaterialId, Material> materials;
    std::unordered_map<scene::MeshId, scene::MeshKey> keys;   // ids are never reused
    std::unordered_map<ItemKey, std::vector<CourseLamp>, ItemKeyHash, ItemKeyEqual> lampsByItem;
};

}  // namespace gt4::render
