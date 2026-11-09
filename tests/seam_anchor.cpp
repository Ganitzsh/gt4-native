// B16c's anchor (platform/gsnative/seam_anchor.h) on synthetic frame lists: GT4's post copy into the back buffer found
// and chained to the Light, through a grade in place; the coverage condition (a small sprite sampling the scene after
// the copy is not the anchor); the map composed through a half-size pass; and the outcomes that leave a picture
// reactive everywhere (fields mode, no covering draw that samples a target, a chain ending at no Light, the cap).
#include "seam_anchor.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {
using namespace gt4;
using Kind = gsn::FrameList::Command::Kind;

int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Targets: 4 the scene (640 x 448) with its Z buffer 5, 12 and 15 the two display buffers, 20 a temporary.
void addTargets(gsn::FrameList& l) {
    auto target = [&](uint32_t id, uint32_t bp, bool depth) {
        gsn::Target t;
        t.id = id, t.bp = bp, t.bw = 10, t.psm = depth ? 1 : 0, t.height = 448, t.depth = depth;
        l.targets.push_back(t);
    };
    target(4, 0x2000, false);
    target(5, 0x3000, true);
    target(12, 0x0000, false);
    target(15, 0x1180, false);
    target(20, 0x4000, false);
}

// The display shows target 12 (progressive unless `fields`), 640 x 448 from (0, 0).
void setDisplay(gsn::FrameList& l, bool fields = false) {
    l.displayed = true;
    l.display.target2 = 12;
    gfx::DisplayCircuit& c = l.display.record.circuit[1];
    c.enabled = 1, c.magH = 4, c.clocks = 2560, c.lines = 448, c.x = 0, c.y = 0;
    l.display.record.interlaced = fields ? 1 : 0;
    l.display.record.fieldMode = fields ? 1 : 0;
}

void light(gsn::FrameList& l, uint32_t target, uint32_t depth) {
    gsn::FrameList::Lighting li;
    li.target = target, li.depth = depth, li.width = 640, li.height = 448;
    l.lights.push_back(li);
    l.commands.push_back({Kind::Light, uint32_t(l.lights.size() - 1)});
}

// A 3D draw into `target` with Z buffer `depth` (modeled: its vertices are the GPU's).
void draw3d(gsn::FrameList& l, uint32_t target, uint32_t depth, bool inverted = false) {
    gsn::Draw d;
    d.target = target, d.depth = depth, d.modeled = true, d.depthInverted = inverted;
    d.state.prim = gfx::Triangles;
    l.draws.push_back(d);
    l.commands.push_back({Kind::Draw, uint32_t(l.draws.size() - 1)});
}

// A sprite into `target` over (x0, y0)-(x1, y1), sampling target `source` (0: untextured) at (u0, v0)-(u1, v1), texel
// coordinates.
void sprite(gsn::FrameList& l, uint32_t target, float x0, float y0, float x1, float y1, uint32_t source = 0, float u0 = 0,
            float v0 = 0, float u1 = 0, float v1 = 0) {
    gsn::Draw d;
    d.target = target;
    d.topology = gsn::Topology::Triangles;
    d.state.prim = gfx::Sprites | (source ? gfx::kTextured | gfx::kTexelCoords : 0u);
    if (source) d.texture.kind = gsn::TextureSource::Kind::Target, d.texture.id = source;
    d.first = uint32_t(l.vertices.size());
    d.count = 6;
    const float xs[6] = {x0, x1, x0, x1, x1, x0}, ys[6] = {y0, y0, y1, y0, y1, y1};
    const float us[6] = {u0, u1, u0, u1, u1, u0}, vs[6] = {v0, v0, v1, v0, v1, v1};
    for (int i = 0; i < 6; ++i) {
        gsn::Vertex v;
        v.x = xs[i], v.y = ys[i], v.u = us[i], v.v = vs[i];
        l.vertices.push_back(v);
    }
    l.draws.push_back(d);
    l.commands.push_back({Kind::Draw, uint32_t(l.draws.size() - 1)});
}

void grade(gsn::FrameList& l, uint32_t source, uint32_t destination) {
    gsn::FrameList::Effect e;
    e.source = source, e.destination = destination, e.x0 = 0, e.y0 = 0, e.x1 = 639, e.y1 = 447;
    l.effects.push_back(e);
    l.commands.push_back({Kind::Effect, uint32_t(l.effects.size() - 1)});
}

}  // namespace

int main() {
    using Outcome = gsn::SeamAnchor::Outcome;
    {   // The race: 3D into 4, its Light, the post copy into the back buffer 15, a grade in place, the HUD.
        gsn::FrameList l;
        addTargets(l);
        setDisplay(l);
        draw3d(l, 4, 5);
        draw3d(l, 4, 5, true);   // (the Z24 pass: the buffer inverted from here)
        light(l, 4, 5);
        sprite(l, 15, 0, 0, 640, 448, 4, 0, 0, 640, 448);   // command 3: the copy
        grade(l, 15, 15);                                   // command 4
        sprite(l, 15, 20, 20, 120, 60);                     // the HUD
        sprite(l, 15, 300, 10, 340, 30, 4, 0, 0, 40, 20);   // a small sprite sampling the scene: not covering
        const gsn::SeamAnchor a = gsn::findSeamAnchor(l);
        check(a.found(), std::string("the race: chained to the Light (") + gsn::seamOutcomeName(a.outcome) + ")");
        check(a.made == 15, "the race: the back buffer is the picture made");
        check(a.at == 4 && a.hops == 2, "the race: the anchor is the grade, two passes back to the Light");
        check(a.lightAt == 2 && a.light == 4 && a.lightDepth == 5, "the race: the Light and its targets");
        check(near(a.scale[0], 1) && near(a.scale[1], 1) && near(a.offset[0], 0) && near(a.offset[1], 0), "the race: one to one");
        check(a.depthInverted, "the race: the Z buffer inverted at the anchor");
        check(near(a.rect[2], 640) && near(a.rect[3], 448), "the race: the displayed rectangle");
        check(a.lastCovering, "the race: the anchor is the last covering pass");
    }
    {   // A half-size temporary between the scene and the copy (a glare composite through 20): the maps compose.
        gsn::FrameList l;
        addTargets(l);
        setDisplay(l);
        draw3d(l, 4, 5);
        light(l, 4, 5);
        sprite(l, 20, 0, 0, 320, 224, 4, 0, 0, 640, 448);   // 4 -> 20 at half size
        sprite(l, 15, 0, 0, 640, 448, 20, 0, 0, 320, 224);  // 20 -> 15, doubled
        sprite(l, 15, 0, 0, 640, 448);                      // a full-screen untextured pass after it (a fade)
        const gsn::SeamAnchor a = gsn::findSeamAnchor(l);
        check(a.found() && a.hops == 2 && a.at == 3, "the temporary: two passes back to the Light");
        check(near(a.scale[0], 1) && near(a.scale[1], 1) && near(a.offset[0], 0), "the temporary: half and double compose to one");
        check(!a.lastCovering, "the temporary: the fade covers after the anchor");
    }
    {   // A copy that reads a part of the scene: the map's scale and offset.
        gsn::FrameList l;
        addTargets(l);
        setDisplay(l);
        draw3d(l, 4, 5);
        light(l, 4, 5);
        sprite(l, 15, 0, 0, 640, 448, 4, 64, 32, 384, 256);
        const gsn::SeamAnchor a = gsn::findSeamAnchor(l);
        check(a.found() && near(a.scale[0], 0.5f) && near(a.scale[1], 0.5f) && near(a.offset[0], 64) && near(a.offset[1], 32),
              "a part of the scene: x / 2 + 64, y / 2 + 32");
    }
    {   // Fields mode: not looked at.
        gsn::FrameList l;
        addTargets(l);
        setDisplay(l, true);
        draw3d(l, 4, 5);
        light(l, 4, 5);
        sprite(l, 15, 0, 0, 640, 448, 4, 0, 0, 640, 448);
        check(gsn::findSeamAnchor(l).outcome == Outcome::Fields, "fields mode");
    }
    {   // A menu: no draw samples a target.
        gsn::FrameList l;
        addTargets(l);
        setDisplay(l);
        sprite(l, 15, 0, 0, 640, 448);
        sprite(l, 15, 100, 100, 200, 120);
        check(gsn::findSeamAnchor(l).outcome == Outcome::NoAnchor, "a menu: no anchor");
    }
    {   // A feedback blur: the copy reads the other display buffer, which no Light drew into.
        gsn::FrameList l;
        addTargets(l);
        setDisplay(l);
        sprite(l, 15, 0, 0, 640, 448, 12, 0, 0, 640, 448);
        check(gsn::findSeamAnchor(l).outcome == Outcome::NoLight, "a feedback blur: no Light");
    }
    {   // Five copies in a chain: the cap.
        gsn::FrameList l;
        addTargets(l);
        setDisplay(l);
        draw3d(l, 4, 5);
        light(l, 4, 5);
        sprite(l, 20, 0, 0, 640, 448, 4, 0, 0, 640, 448);
        for (int i = 0; i < 4; ++i) sprite(l, 20, 0, 0, 640, 448, 20, 0, 0, 640, 448);   // (in place: one pass of its own)
        sprite(l, 15, 0, 0, 640, 448, 20, 0, 0, 640, 448);
        const gsn::SeamAnchor a = gsn::findSeamAnchor(l);
        // (the in-place copies are one pass of their own, so this chain is 3 passes: found)
        check(a.found() && a.hops == 3, "a chain of in-place copies: three passes");
    }
    {   // Five passes through five targets: the cap.
        gsn::FrameList l;
        addTargets(l);
        for (uint32_t id : {21u, 22u, 23u}) {
            gsn::Target t;
            t.id = id, t.bp = 0x5000 + id * 0x100, t.bw = 10, t.height = 448;
            l.targets.push_back(t);
        }
        setDisplay(l);
        draw3d(l, 4, 5);
        light(l, 4, 5);
        sprite(l, 20, 0, 0, 640, 448, 4, 0, 0, 640, 448);
        sprite(l, 21, 0, 0, 640, 448, 20, 0, 0, 640, 448);
        sprite(l, 22, 0, 0, 640, 448, 21, 0, 0, 640, 448);
        sprite(l, 23, 0, 0, 640, 448, 22, 0, 0, 640, 448);
        sprite(l, 15, 0, 0, 640, 448, 23, 0, 0, 640, 448);
        const gsn::SeamAnchor a = gsn::findSeamAnchor(l);
        check(a.outcome == Outcome::Capped && a.hops == gsn::kSeamHops, "five passes: the cap");
    }
    {   // A list cut for a GS read shows nothing.
        gsn::FrameList l;
        addTargets(l);
        check(gsn::findSeamAnchor(l).outcome == Outcome::NotShown, "a list cut for a read");
    }
    if (failures) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::printf("seam_anchor: all checks passed\n");
    return 0;
}
