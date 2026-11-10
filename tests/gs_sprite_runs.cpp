// The front end's sprite runs (platform/gsnative/gs_frontend.h SpriteRun): sprites in one draw that continue one image
// (each one's screen rectangle abutting the previous one's and its UV rectangle starting where that one's ends) share
// the run's union as their texture bounds, so gsn.frag's insideSprite clamps the run as a whole above 1:1 instead of
// each slice (the night's 32-px strips: GT4's light image stretched over the road in 20 slices). A sprite that doesn't
// continue the one before it keeps its own bounds. CPU only: GS packets in, the frame list's vertices checked.
#include "gs_frontend.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using gt4::gsn::FrameList;
using gt4::gsn::GsFrontend;

namespace {

int failures = 0;
void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what.c_str());
        ++failures;
    }
}

// One GIF packet of A+D register writes (PACKED, NREG 1, the register in each qword's upper half).
struct Packet {
    std::vector<uint64_t> words;
    void reg(uint32_t address, uint64_t value) {
        words.push_back(value);
        words.push_back(address);
    }
    std::vector<uint8_t> bytes() const {
        const uint64_t nloop = words.size() / 2;
        std::vector<uint64_t> all = {nloop | (1ull << 15) | (1ull << 60), 0xEull};   // EOP, NREG 1, A+D
        all.insert(all.end(), words.begin(), words.end());
        std::vector<uint8_t> out(all.size() * 8);
        std::memcpy(out.data(), all.data(), out.size());
        return out;
    }
};

constexpr uint32_t PRIM = 0x00, UV = 0x03, XYZ2 = 0x05, TEX0_1 = 0x06, XYOFFSET_1 = 0x18, SCISSOR_1 = 0x40,
                   TEST_1 = 0x47, FRAME_1 = 0x4C, ZBUF_1 = 0x4E;

uint64_t uv(float u, float v) { return uint64_t(u * 16.0f) | (uint64_t(v * 16.0f) << 16); }
uint64_t xyz(float x, float y) { return uint64_t(x * 16.0f) | (uint64_t(y * 16.0f) << 16); }

// A textured sprite from (x0, y0) with UV (u0, v0) to (x1, y1) with UV (u1, v1)
void sprite(Packet& p, float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1) {
    p.reg(UV, uv(u0, v0));
    p.reg(XYZ2, xyz(x0, y0));
    p.reg(UV, uv(u1, v1));
    p.reg(XYZ2, xyz(x1, y1));
}

}  // namespace

int main() {
    GsFrontend fe;
    Packet p;
    p.reg(FRAME_1, (10ull << 16));                       // FBP 0, FBW 10 (640), PSMCT32
    p.reg(ZBUF_1, (1ull << 32));                         // (ZMSK: no Z written)
    p.reg(TEST_1, 0);                                    // no tests
    p.reg(XYOFFSET_1, 0);
    p.reg(SCISSOR_1, (639ull << 16) | (447ull << 48));   // x 0..639, y 0..447
    p.reg(TEX0_1, (0x3000ull) | (1ull << 14) | (5ull << 26) | (5ull << 30) | (1ull << 34));   // TBP0, TBW 1, 32 x 32, TCC
    p.reg(PRIM, 6 | (1u << 4) | (1u << 8));             // sprite, textured, UV
    // A run of three slices continuing one image: x 0..32..64..96, u 0..1.5..3..4.5, v 0..28 over the full height
    sprite(p, 0, 0, 0.0f, 0.0f, 32, 448, 1.5f, 28.0f);
    sprite(p, 32, 0, 1.5f, 0.0f, 64, 448, 3.0f, 28.0f);
    sprite(p, 64, 0, 3.0f, 0.0f, 96, 448, 4.5f, 28.0f);
    // Abutting on screen, but its UV doesn't continue (a gap in the texture): its own bounds, and a new run
    sprite(p, 96, 0, 6.0f, 0.0f, 128, 448, 7.5f, 28.0f);
    // An isolated sprite elsewhere (a glyph): its own bounds
    sprite(p, 300, 100, 10.0f, 2.0f, 316, 116, 12.0f, 4.0f);
    const std::vector<uint8_t> b = p.bytes();
    fe.gif(2, b.data(), b.size());
    FrameList list;
    fe.vsync(1, list);

    expect(list.vertices.size() == 5 * 6, "five sprites, six vertices each (got " + std::to_string(list.vertices.size()) + ")");
    auto bounds = [&](size_t sprite, float minU, float maxU, float minV, float maxV, const std::string& what) {
        for (size_t k = 0; k < 6 && sprite * 6 + k < list.vertices.size(); ++k) {
            const auto& v = list.vertices[sprite * 6 + k];
            const bool ok = std::fabs(v.minU - minU) < 1e-4f && std::fabs(v.maxU - maxU) < 1e-4f &&
                            std::fabs(v.minV - minV) < 1e-4f && std::fabs(v.maxV - maxV) < 1e-4f;
            if (!ok) {
                expect(false, what + ": vertex " + std::to_string(k) + " has " + std::to_string(v.minU) + ".." +
                                  std::to_string(v.maxU) + " x " + std::to_string(v.minV) + ".." + std::to_string(v.maxV));
                return;
            }
        }
    };
    if (list.vertices.size() == 30) {
        for (size_t s = 0; s < 3; ++s) bounds(s, 0.0f, 4.5f, 0.0f, 28.0f, "the run's slice " + std::to_string(s) + " takes the run's union");
        bounds(3, 6.0f, 7.5f, 0.0f, 28.0f, "a slice whose UV doesn't continue keeps its own bounds");
        bounds(4, 10.0f, 12.0f, 2.0f, 4.0f, "an isolated sprite keeps its own bounds");
    }
    expect(fe.stats.spriteRuns == 1 && fe.stats.spritesInRuns == 3,
           "one run of three sprites counted (got " + std::to_string(fe.stats.spriteRuns) + " runs, " +
               std::to_string(fe.stats.spritesInRuns) + " sprites)");

    // A vertical run: two slices one above the other, the same x and u range, v continuing
    {
        GsFrontend f2;
        Packet q;
        q.reg(FRAME_1, (10ull << 16));
        q.reg(ZBUF_1, (1ull << 32));
        q.reg(TEST_1, 0);
        q.reg(XYOFFSET_1, 0);
        q.reg(SCISSOR_1, (639ull << 16) | (447ull << 48));
        q.reg(TEX0_1, (0x3000ull) | (1ull << 14) | (5ull << 26) | (5ull << 30) | (1ull << 34));
        q.reg(PRIM, 6 | (1u << 4) | (1u << 8));
        sprite(q, 0, 0, 0.0f, 0.0f, 640, 224, 28.0f, 14.0f);
        sprite(q, 0, 224, 0.0f, 14.0f, 640, 448, 28.0f, 28.0f);
        const std::vector<uint8_t> qb = q.bytes();
        f2.gif(2, qb.data(), qb.size());
        FrameList l2;
        f2.vsync(1, l2);
        list = std::move(l2);
        expect(list.vertices.size() == 12, "two vertical slices drawn");
        if (list.vertices.size() == 12) {
            bounds(0, 0.0f, 28.0f, 0.0f, 28.0f, "the vertical run's top slice takes the union");
            bounds(1, 0.0f, 28.0f, 0.0f, 28.0f, "the vertical run's bottom slice takes the union");
        }
    }

    if (failures) return 1;
    std::printf("gs_sprite_runs: all checks passed\n");
    return 0;
}
