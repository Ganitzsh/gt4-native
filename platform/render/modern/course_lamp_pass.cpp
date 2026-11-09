// F2b's lamp pass (course_lamp_pass.h).
#include "course_lamp_pass.h"

#include "glow_classifier.h"   // kMaterialGlow, glowColour

#include <algorithm>
#include <cmath>
#include <cstdio>    // LOCAL F2b DEBUG
#include <cstdlib>   // LOCAL F2b DEBUG

namespace gt4::render {

void CourseLampPass::promote(const scene::SceneFrame& frame, uint16_t view,
                             const std::function<uint32_t(scene::MaterialId)>& classes, std::vector<gfx::scene::Light>& records,
                             Stats* stats) {
    using namespace gfx::scene;
    Stats s;
    if (classesById.size() > 100000) classesById.clear();   // (ids are never reused: this grows with the ids dropped)
    bySerial.clear();
    for (uint32_t i = 0; i < frame.instances.size(); ++i) bySerial.emplace(frame.instances[i].serial, i);
    // the material class of an instance: its first item's (one billboard type per flush, checked by A7 step 1b)
    auto glowOf = [&](const scene::InstanceData& inst, uint32_t& out) {
        if (!inst.materialCount || inst.firstMaterial >= frame.materials.size()) return false;
        const scene::MaterialId id = frame.materials[inst.firstMaterial];
        auto known = classesById.find(id);
        if (known == classesById.end()) known = classesById.emplace(id, classes ? classes(id) : 0u).first;
        out = known->second;
        return true;
    };
    const size_t count = records.size();
    // the game's own course lamps of the view (entries2[]), gathered before any promotion: a candidate merges only with
    // those, never with another candidate promoted in this call (two lamp heads on one post stay two)
    gameLamps.clear();
    mergedInto.assign(count, 0);
    for (size_t g = 0; g < count; ++g)
        if (records[g].source == kSrcCourseGlow && records[g].view == view) gameLamps.push_back(g);
    for (size_t r = 0; r < count; ++r) {
        Light& c = records[r];
        if (c.source != kSrcBillboard || c.view != view) continue;
        ++s.candidates;
        auto at = bySerial.find(c.id);
        uint32_t bits = 0;
        if (at == bySerial.end() || !glowOf(frame.instances[at->second], bits)) {
            ++s.noInstance;
            continue;
        }
        if (!(bits & kMaterialGlow)) {
            ++s.dropped;
            static const bool dropLog = [] { const char* e = std::getenv("GT4_F2B_DROPLOG"); return e && *e == '1'; }();
            if (dropLog)   // LOCAL F2b DEBUG, never committed: each dropped candidate's frame and material
                std::printf("F2b drop: frame %u vblank %u material %u\n", frame.frame, frame.vblank,
                            unsigned(frame.materials[frame.instances[at->second].firstMaterial]));
            continue;
        }
        // one lamp with a game record (entries2[]) within the billboard's half size (Light's width and height are half
        // sizes: gfx.h), the larger of the two: the record's position and blink, the billboard's size
        const float reach = std::max(c.width, c.height);
        bool merged = false;
        for (size_t g : gameLamps) {
            if (merged) break;
            Light& game = records[g];
            const float dx = game.position[0] - c.position[0], dy = game.position[1] - c.position[1],
                        dz = game.position[2] - c.position[2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) <= reach) {
                if (mergedInto[g]) ++s.mergedAgain;   // (a second head at one record: one light)
                mergedInto[g] = 1;
                game.width = c.width;
                game.height = c.height;
                merged = true;
            }
        }
        if (merged) {
            ++s.merged;
            continue;   // (stays a candidate: not lit twice)
        }
        // promoted: the glow texture's mean (in the class bits) times the billboard's own colour (A7 step 1b: its four
        // vertex colours' mean, 0x80 = 1, so a channel may reach 2; white when the game has none)
        float mean[3];
        glowColour(bits, mean);
        for (int k = 0; k < 3; ++k) c.colour[k] = mean[k] * c.colour[k];
        c.source = kSrcCourseGlow;
        ++s.kept;
    }
    if (stats) *stats = s;
}

}  // namespace gt4::render
