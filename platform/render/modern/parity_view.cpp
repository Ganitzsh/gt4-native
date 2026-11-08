// B7's parity path, its planning on the CPU (parity_view.h).
#include "parity_view.h"

#include <algorithm>
#include <cstring>

namespace gt4::render {

void ParityView::setArena(scene::MeshRawVertex* v, uint32_t vCapacity, uint32_t* i, uint32_t iCapacity, bool rotateTriangles) {
    vertices = v;
    vertexCapacity = vCapacity;
    indices = i;
    indexCapacity = iCapacity;
    rotate = rotateTriangles;
    reset();
}

void ParityView::reset() {
    placed.clear();
    vertexUsed = indexUsed = 0;
    full = false;
}

const ParityView::Placed* ParityView::place(const scene::Mesh& mesh, scene::MeshId id) {
    if (auto it = placed.find(id); it != placed.end()) return &it->second;
    const size_t v = mesh.raw.size(), n = mesh.indices.size();
    if (v != mesh.vertices.size() || n % 3 || mesh.turned.size() != n / 3) return nullptr;
    if (!vertices || vertexUsed + v > vertexCapacity || indexUsed + n > indexCapacity) {
        full = true;
        return nullptr;
    }
    Placed p;
    p.vertexOffset = vertexUsed;
    p.firstIndex = indexUsed;
    p.items = mesh.items;
    std::memcpy(vertices + vertexUsed, mesh.raw.data(), v * sizeof(scene::MeshRawVertex));
    uint32_t* out = indices + indexUsed;
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
    vertexUsed += static_cast<uint32_t>(v);
    indexUsed += static_cast<uint32_t>(n);
    ++counters.meshesPlaced;
    return &placed.emplace(id, std::move(p)).first->second;
}

bool ParityView::plan(const gsn::FrameList& list, std::vector<ParitySpan>& spans) {
    spans.clear();
    ParityStats seen;   // (counted only when the plan completes: a plan redone after a reset counts once)
    for (const gsn::FrameList::ViewOnList& view : list.views) {
        if (!view.package) continue;
        if (view.ids.empty() || view.items.empty()) {   // cut or not valid: wholly on the GS path (its items unknown)
            ++seen.viewsNotSubstituted;
            continue;
        }
        for (const scene::InstanceIds& ids : view.ids) {
            seen.items += ids.materialCount;
            if (!ids.mesh) {
                seen.meshNone += ids.materialCount;
                continue;
            }
            const Placed* p = nullptr;
            if (auto it = placed.find(ids.mesh); it != placed.end()) {
                p = &it->second;
            } else {
                const std::shared_ptr<const scene::Mesh> mesh = list.sceneCache ? list.sceneCache->meshes().mesh(ids.mesh) : nullptr;
                if (!mesh) {
                    seen.meshGone += ids.materialCount;
                    continue;
                }
                p = place(*mesh, ids.mesh);
                if (!p) {
                    if (full) return false;
                    seen.itemsDiffer += ids.materialCount;
                    continue;
                }
            }
            if (p->items.size() != ids.materialCount || size_t(ids.firstMaterial) + ids.materialCount > view.items.size()) {
                seen.itemsDiffer += ids.materialCount;
                continue;
            }
            for (uint32_t j = 0; j < ids.materialCount; ++j) {
                const gsn::FrameList::ViewOnList::ItemRef& ref = view.items[ids.firstMaterial + j];
                const scene::MeshItem& item = p->items[j];
                if (item.animated || ref.job >= list.models.size() || ref.draw >= list.draws.size() ||
                    item.indexCount != ref.modelVertexCount) {
                    ++seen.itemsDiffer;
                    continue;
                }
                ParitySpan s;
                s.draw = ref.draw;
                s.first = ref.firstModelVertex;
                s.count = ref.modelVertexCount;
                s.job = ref.job;
                s.renderer = item.renderer;
                s.vertexOffset = p->vertexOffset;
                s.firstIndex = p->firstIndex + item.firstIndex;
                s.indexCount = item.indexCount;
                spans.push_back(s);
                ++seen.drawn;
            }
        }
    }
    std::sort(spans.begin(), spans.end(), [](const ParitySpan& a, const ParitySpan& b) {
        return a.draw != b.draw ? a.draw < b.draw : a.first < b.first;
    });
    counters.items += seen.items;
    counters.drawn += seen.drawn;
    counters.viewsNotSubstituted += seen.viewsNotSubstituted;
    counters.meshNone += seen.meshNone;
    counters.meshGone += seen.meshGone;
    counters.itemsDiffer += seen.itemsDiffer;
    return true;
}

}  // namespace gt4::render
