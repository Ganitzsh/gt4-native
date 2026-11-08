#pragma once
// The material table (docs/modern/ARCHITECTURE.md's scene cache, ticket B6): GT4's materials by MaterialKey
// (docs/modern/CONTRACTS.md a.8: what a batch's pixels depend on besides its geometry) as small stable ids. The same
// key gets the same id in every frame while the table holds it; a key not asked for in `unusedFrames` frames is
// dropped, and asked for again it gets a new id (ids are never reused). The modern views draw by id (B7) and look the
// key up for what it holds.
//
// Its writer (the scene cache, on the front-end thread) and its readers (the render thread) may differ: every call
// takes the table's lock.
#include "scene_frame.h"   // MaterialKey, MaterialId

#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gt4::scene {

struct MaterialKeyHash {
    size_t operator()(const MaterialKey& k) const;
};
struct MaterialKeyEqual {
    bool operator()(const MaterialKey& a, const MaterialKey& b) const { return std::memcmp(&a, &b, sizeof a) == 0; }
};
static_assert(sizeof(MaterialKey) == 32, "no padding: keys are hashed and compared as bytes");

class MaterialTable {
public:
    // The id for this key (0 never): its own while held, a new one the first time (counted in Stats::created). A new id
    // takes the class bits `classify` gives (called only then, under the table's lock), kept with it (classes()).
    MaterialId id(const MaterialKey& key, const std::function<uint32_t()>& classify = {}, bool* made = nullptr);
    uint32_t classes(MaterialId id) const;   // 0: none, or unknown
    // The key an id stands for; false when the id is unknown or dropped.
    bool key(MaterialId id, MaterialKey& out) const;

    // The end of a frame: keys not asked for in the last `unusedFrames` frames are dropped.
    void endFrame();
    void setUnusedFrames(uint32_t frames);

    struct Stats {
        uint64_t requests = 0, created = 0, dropped = 0;
        uint64_t held = 0;   // now
    };
    Stats stats() const;

private:
    struct Entry {
        MaterialKey key{};
        uint32_t lastUsed = 0;
        uint32_t classes = 0;
        bool dropped = false;
    };
    mutable std::mutex lock;
    uint32_t frame = 0, unusedFrames = 300;
    std::unordered_map<MaterialKey, MaterialId, MaterialKeyHash, MaterialKeyEqual> ids;
    std::vector<Entry> entries;   // index = id - 1
    Stats counters;
};

}  // namespace gt4::scene
