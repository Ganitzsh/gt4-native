// The material table (material_table.h).
#include "material_table.h"

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"

namespace gt4::scene {

size_t MaterialKeyHash::operator()(const MaterialKey& k) const { return static_cast<size_t>(XXH3_64bits(&k, sizeof k)); }

MaterialId MaterialTable::id(const MaterialKey& key, const std::function<uint32_t()>& classify) {
    std::lock_guard<std::mutex> guard(lock);
    ++counters.requests;
    auto it = ids.find(key);
    if (it != ids.end()) {
        entries[it->second - 1].lastUsed = frame;
        return it->second;
    }
    entries.push_back(Entry{key, frame, classify ? classify() : 0u, false});
    const MaterialId made = static_cast<MaterialId>(entries.size());
    ids.emplace(key, made);
    ++counters.created;
    ++counters.held;
    return made;
}

uint32_t MaterialTable::classes(MaterialId id) const {
    std::lock_guard<std::mutex> guard(lock);
    return id && id <= entries.size() && !entries[id - 1].dropped ? entries[id - 1].classes : 0u;
}

bool MaterialTable::key(MaterialId id, MaterialKey& out) const {
    std::lock_guard<std::mutex> guard(lock);
    if (id == 0 || id > entries.size() || entries[id - 1].dropped) return false;
    out = entries[id - 1].key;
    return true;
}

void MaterialTable::endFrame() {
    std::lock_guard<std::mutex> guard(lock);
    ++frame;
    for (Entry& e : entries) {
        if (e.dropped || frame - e.lastUsed <= unusedFrames) continue;
        ids.erase(e.key);   // asked for again: a new id
        e.dropped = true;
        ++counters.dropped;
        --counters.held;
    }
}

void MaterialTable::setUnusedFrames(uint32_t frames) {
    std::lock_guard<std::mutex> guard(lock);
    unusedFrames = frames;
}

MaterialTable::Stats MaterialTable::stats() const {
    std::lock_guard<std::mutex> guard(lock);
    return counters;
}

}  // namespace gt4::scene
