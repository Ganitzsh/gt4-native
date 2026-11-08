// B7's parity path and B8's hybrid, their planning on the CPU (parity_view.h).
#include "parity_view.h"

#include <algorithm>
#include <cstring>

namespace gt4::render {

bool BlockRanges::allocate(uint64_t bytes, uint64_t alignment, Range& out) {
    if (bytes == 0 || bytes > blockBytes) return false;
    auto fit = [&](uint32_t b) {
        auto& free = freeLists[b];
        for (auto it = free.begin(); it != free.end(); ++it) {
            const uint64_t start = (it->first + alignment - 1) / alignment * alignment;
            const uint64_t end = it->first + it->second;
            if (start + bytes > end) continue;
            // split the free range around [start, start + bytes)
            const uint64_t before = start - it->first, after = end - (start + bytes);
            const uint64_t at = it->first;
            free.erase(it);
            if (before) free.emplace(at, before);
            if (after) free.emplace(start + bytes, after);
            out = {b, start, bytes};
            usedBytes += bytes;
            return true;
        }
        return false;
    };
    for (uint32_t b = 0; b < freeLists.size(); ++b)
        if (fit(b)) return true;
    if (freeLists.size() >= maxBlocks) return false;
    freeLists.emplace_back();
    freeLists.back().emplace(0, blockBytes);
    return fit(static_cast<uint32_t>(freeLists.size() - 1));
}

void BlockRanges::release(const Range& range) {
    if (range.block >= freeLists.size() || !range.bytes) return;
    auto& free = freeLists[range.block];
    uint64_t start = range.offset, end = range.offset + range.bytes;
    // merge with the free neighbours
    auto next = free.lower_bound(start);
    if (next != free.end() && next->first == end) {
        end += next->second;
        next = free.erase(next);
    }
    if (next != free.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == start) {
            start = prev->first;
            free.erase(prev);
        }
    }
    free.emplace(start, end - start);
    usedBytes -= range.bytes;
}

ParityView::ParityView(uint64_t blockBytes, uint32_t maxBlocks, uint32_t unusedFrames, uint32_t framesInFlight)
    : ranges(blockBytes, maxBlocks), unusedFrames(unusedFrames), framesInFlight(framesInFlight) {}

bool ParityView::makeRoom(uint64_t bytes) {
    // the least recently used first, never one drawn within the frames in flight (this frame's included)
    std::vector<std::pair<uint64_t, scene::MeshId>> byAge;
    for (const auto& [id, p] : placed)
        if (p.lastUsed + framesInFlight <= frame) byAge.emplace_back(p.lastUsed, id);
    std::sort(byAge.begin(), byAge.end());
    uint64_t freed = 0;
    for (const auto& [age, id] : byAge) {
        if (freed >= bytes) break;
        auto it = placed.find(id);
        freed += it->second.range.bytes;
        retired.emplace_back(frame, it->second.range);
        placed.erase(it);
        ++counters.meshesRetired;
    }
    return freed >= bytes;   // (the ranges come free framesInFlight frames from now: endFrame)
}

const ParityView::Placed* ParityView::place(const scene::Mesh& mesh, scene::MeshId id, std::vector<uint8_t>& staging,
                                            std::vector<ParityCopy>& copies) {
    if (auto it = placed.find(id); it != placed.end()) {
        it->second.lastUsed = frame;
        return &it->second;
    }
    const size_t v = mesh.raw.size(), n = mesh.indices.size();
    if (v != mesh.vertices.size() || n % 3 || mesh.turned.size() != n / 3 || !v) return nullptr;
    const uint64_t vertexBytes = uint64_t(v) * sizeof(scene::MeshRawVertex);
    const uint64_t indexAt = (vertexBytes + 15) / 16 * 16;
    const uint64_t bytes = indexAt + uint64_t(n) * 4;
    BlockRanges::Range range;
    if (!ranges.allocate(bytes, 16, range)) {
        makeRoom(bytes);
        ++counters.overBudget;   // (this frame on the GS path; the retired ranges come free in a few frames)
        return nullptr;
    }
    Placed p;
    p.range = range;
    p.indexOffset = range.offset + indexAt;
    p.items = mesh.items;
    p.lastUsed = frame;
    // the staging stream: the raw vertices, padding, then the triangles in the GS path's order
    const size_t at = staging.size();
    staging.resize(at + bytes, 0);
    std::memcpy(staging.data() + at, mesh.raw.data(), vertexBytes);
    uint32_t* out = reinterpret_cast<uint32_t*>(staging.data() + at + indexAt);
    for (size_t t = 0; t < n; t += 3) {
        uint32_t a = mesh.indices[t], b = mesh.indices[t + 1];
        const uint32_t c = mesh.indices[t + 2];
        if (mesh.turned[t / 3]) std::swap(a, b);   // (the GS path's order: (v - 2, v - 1, v))
        if (rotate) {   // (a, b, v) -> (v, a, b): the strip's last vertex first, the winding kept
            out[t] = c;
            out[t + 1] = a;
            out[t + 2] = b;
        } else {
            out[t] = a;
            out[t + 1] = b;
            out[t + 2] = c;
        }
    }
    copies.push_back({at, range.offset, bytes, range.block});
    ++counters.meshesPlaced;
    counters.uploadBytes += bytes;
    return &placed.emplace(id, std::move(p)).first->second;
}

void ParityView::plan(const gsn::FrameList& list, std::vector<ParitySpan>& spans, std::vector<uint8_t>& staging,
                      std::vector<ParityCopy>& copies) {
    spans.clear();
    for (uint32_t viewIndex = 0; viewIndex < list.views.size(); ++viewIndex) {
        const gsn::FrameList::ViewOnList& view = list.views[viewIndex];
        if (!view.package) continue;
        // Roles never substituted (CONTRACTS.md a.5): the HUD's 3D, menus' 3D, and an env face's copy into the shared env
        // map (kViewEnvCopy: after the face's ViewEnd, pass = face): the GS path draws them.
        const uint8_t role = view.package->view.role;
        if (role == gfx::scene::kViewHud || role == gfx::scene::kViewMenu || role == gfx::scene::kViewEnvCopy) {
            ++counters.viewsOfRoleKept;
            continue;
        }
        if (view.ids.empty() || view.items.empty()) {   // cut or not valid: wholly on the GS path (its items unknown)
            ++counters.viewsNotSubstituted;
            continue;
        }
        if (readSwitch) {   // (B8 step 4: a game read sees the PS2's picture)
            // An asynchronous read cuts its list too (not displayed), so the readbacks decide first: only the views of
            // the target they name. A list cut with none was cut for a synchronous read (its target unknown here).
            if (!list.readbacks.empty()) {
                bool read = false;
                for (const gsn::FrameList::Readback& r : list.readbacks) read = read || r.request.target == view.target;
                if (read) {
                    ++counters.viewsForReadAsync;
                    continue;
                }
            } else if (!list.displayed) {
                ++counters.viewsForReadSync;
                continue;
            }
        }
        for (uint32_t k = 0; k < view.ids.size(); ++k) {
            const scene::InstanceIds& ids = view.ids[k];
            // A car's light map and the road reflection (kInstLightMap, kInstReflection) are out of the binding and
            // never substituted (CONTRACTS a.5.7): render-to-texture the GS path builds in place. Their instances are
            // the view's (the front end gathers items by instance, not by the view word), so they are left out here.
            const uint16_t flags = k < view.package->instances.size() ? view.package->instances[k].flags : 0;
            if (flags & (gfx::scene::kInstLightMap | gfx::scene::kInstReflection)) {
                counters.itemsOutOfBinding += ids.materialCount;
                continue;
            }
            counters.items += ids.materialCount;
            if (!ids.mesh) {
                counters.meshNone += ids.materialCount;
                continue;
            }
            const Placed* p = nullptr;
            if (auto it = placed.find(ids.mesh); it != placed.end()) {
                it->second.lastUsed = frame;
                p = &it->second;
            } else {
                const std::shared_ptr<const scene::Mesh> mesh = list.sceneCache ? list.sceneCache->meshes().mesh(ids.mesh) : nullptr;
                if (!mesh) {
                    counters.meshGone += ids.materialCount;
                    continue;
                }
                const uint64_t before = counters.overBudget;
                p = place(*mesh, ids.mesh, staging, copies);
                if (!p) {
                    if (counters.overBudget != before) counters.overBudget += ids.materialCount - 1;   // (items, not meshes)
                    else counters.itemsDiffer += ids.materialCount;
                    continue;
                }
            }
            if (p->items.size() != ids.materialCount || size_t(ids.firstMaterial) + ids.materialCount > view.items.size()) {
                counters.itemsDiffer += ids.materialCount;
                continue;
            }
            for (uint32_t j = 0; j < ids.materialCount; ++j) {
                const gsn::FrameList::ViewOnList::ItemRef& ref = view.items[ids.firstMaterial + j];
                const scene::MeshItem& item = p->items[j];
                if (item.animated || ref.job >= list.models.size() || ref.draw >= list.draws.size() ||
                    item.indexCount != ref.modelVertexCount) {
                    ++counters.itemsDiffer;
                    continue;
                }
                ParitySpan s;
                s.draw = ref.draw;
                s.first = ref.firstModelVertex;
                s.count = ref.modelVertexCount;
                s.job = ref.job;
                s.renderer = item.renderer;
                s.block = p->range.block;
                s.vertexOffset = p->range.offset;
                s.indexOffset = p->indexOffset;
                s.firstIndex = item.firstIndex;
                s.indexCount = item.indexCount;
                s.view = viewIndex;
                s.instance = k;
                s.material = ids.firstMaterial + j < view.materials.size() ? view.materials[ids.firstMaterial + j] : 0;
                spans.push_back(s);
                ++counters.drawn;
            }
        }
    }
    std::sort(spans.begin(), spans.end(), [](const ParitySpan& a, const ParitySpan& b) {
        return a.draw != b.draw ? a.draw < b.draw : a.first < b.first;
    });
}

void ParityView::endFrame() {
    // retired framesInFlight frames ago: no frame in flight reads them now
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [&](const std::pair<uint64_t, BlockRanges::Range>& r) {
                                     if (r.first + framesInFlight > frame) return false;
                                     ranges.release(r.second);
                                     return true;
                                 }),
                  retired.end());
    // meshes not drawn for unusedFrames
    for (auto it = placed.begin(); it != placed.end();) {
        if (it->second.lastUsed + unusedFrames < frame) {
            retired.emplace_back(frame, it->second.range);
            ++counters.meshesRetired;
            it = placed.erase(it);
        } else {
            ++it;
        }
    }
    ++frame;
}

}  // namespace gt4::render
