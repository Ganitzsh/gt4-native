// F2b's lamp pass (course_lamp_pass.h).
#include "course_lamp_pass.h"

#include "glow_classifier.h"   // kMaterialGlow

#include <algorithm>
#include <cmath>

namespace gt4::render {

void CourseLampPass::lights(const scene::SceneFrame& frame, uint16_t view, const LampSources& sources,
                            std::vector<gfx::scene::Light>& out, Stats* stats) {
    Stats s;
    // Bounded: material and mesh ids are never reused, so these grow with the ids dropped; the lamps are keyed by
    // content. Cleared past their bounds (refilled from the tables and the meshes at the next sight).
    if (materials.size() > 100000) materials.clear();
    if (keys.size() > 100000) keys.clear();
    if (lampsByItem.size() > 4096) lampsByItem.clear();
    if (!view || view > frame.views.size()) {
        if (stats) *stats = s;
        return;
    }
    const scene::ViewData& v = frame.views[view - 1u];
    for (uint32_t i = 0; i < v.instanceCount; ++i) {
        const uint32_t at = v.firstInstance + i;
        if (at >= frame.instances.size()) break;
        const scene::InstanceData& inst = frame.instances[at];
        if (!inst.mesh || inst.view != view) continue;
        ++s.instances;
        std::shared_ptr<const scene::Mesh> mesh;   // fetched once per instance, only when needed
        for (uint32_t k = 0; k < inst.materialCount; ++k) {
            const uint32_t m = inst.firstMaterial + k;
            if (m >= frame.materials.size()) break;
            const scene::MaterialId id = frame.materials[m];
            auto known = materials.find(id);
            if (known == materials.end()) {   // (first sight on the render thread: the table's lock once per id)
                Material entry;
                entry.classes = sources.classes ? sources.classes(id) : 0u;
                if ((entry.classes & kMaterialGlow) && !(sources.glowColour && sources.glowColour(id, entry.colour)))
                    entry.colour[0] = entry.colour[1] = entry.colour[2] = 1.0f;
                known = materials.emplace(id, entry).first;
            }
            const Material& material = known->second;
            if (!(material.classes & kMaterialGlow)) continue;
            ++s.glowItems;
            // the item's lamps, by the mesh's content and the item
            auto key = keys.find(inst.mesh);
            if (key == keys.end()) {
                if (!mesh && sources.mesh) mesh = sources.mesh(inst.mesh);
                if (!mesh) {
                    ++s.meshMissing;
                    continue;
                }
                key = keys.emplace(inst.mesh, mesh->key).first;
            }
            const ItemKey itemKey{key->second, k};
            auto lamps = lampsByItem.find(itemKey);
            if (lamps == lampsByItem.end()) {
                if (!mesh && sources.mesh) mesh = sources.mesh(inst.mesh);
                if (!mesh) {
                    ++s.meshMissing;
                    continue;
                }
                LampStats ls;
                lamps = lampsByItem.emplace(itemKey, extractLamps(*mesh, k, 0.25f, &ls)).first;
                ++s.extracted;
                s.largestItemQuads = std::max(s.largestItemQuads, ls.quads);
                s.skewQuads += ls.skewQuads;
                s.degenerate += ls.degenerate;
            } else {
                ++s.cacheHits;
            }
            // object space -> view space: p x diag(axisScale x k, 1) x objectToView (GT4's row vectors; k = shapeScale, 1
            // when 0), the radius by the largest of the scaled axes
            const float shape = inst.shapeScale != 0.0f ? inst.shapeScale : 1.0f;
            const float (&o)[4][4] = inst.objectToView.m;
            float axis[3];
            for (int a = 0; a < 3; ++a) axis[a] = inst.axisScale[a] * shape;
            float reach = 0.0f;
            for (int a = 0; a < 3; ++a)
                reach = std::max(reach, std::fabs(axis[a]) * std::sqrt(o[a][0] * o[a][0] + o[a][1] * o[a][1] + o[a][2] * o[a][2]));
            uint32_t n = 0;
            for (const CourseLamp& lamp : lamps->second) {
                gfx::scene::Light l{};
                l.prefix = {gfx::scene::kLight, gfx::scene::kVersion};
                l.view = view;
                l.kind = gfx::scene::kLightPoint;
                l.source = gfx::scene::kSrcCourseGlow;
                l.id = static_cast<uint32_t>(inst.object * 2654435761u) ^ (k << 24) ^ n++;
                l.entrant = 0xFFFFFFFFu;
                const float p[3] = {lamp.position[0] * axis[0], lamp.position[1] * axis[1], lamp.position[2] * axis[2]};
                for (int c = 0; c < 3; ++c) l.position[c] = p[0] * o[0][c] + p[1] * o[1][c] + p[2] * o[2][c] + o[3][c];
                l.width = l.height = lamp.radius * reach;
                for (int c = 0; c < 3; ++c) l.colour[c] = material.colour[c] * lamp.colour[c];
                l.intensity = 1.0f;
                out.push_back(l);
                ++s.lamps;
            }
        }
    }
    if (stats) *stats = s;
}

}  // namespace gt4::render
