// N3's lit-road rule (platform/scene/lit_road.h): the layer's draw state, and v4's measures and verdict on made-up
// items: a lamp pool passes; a road marking (too narrow), the start/finish checkers (too fine a texel), a wall (not
// up-facing), a dim item, a glow texture and an item without a texture summary don't. A pool in two pieces that share
// positions but not indices is one piece (welded), a pool turned on its side is not up-facing, and a moved pool is the
// same pool.
#include "lit_road.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

using gt4::scene::DecodedBatch;
using gt4::scene::MeshVertex;

MeshVertex vertex(float x, float y, float z, float s, float t, uint32_t colour) {
    MeshVertex v;
    v.position[0] = x;
    v.position[1] = y;
    v.position[2] = z;
    v.st[0] = s;
    v.st[1] = t;
    v.colour = colour;
    return v;
}

// A flat rectangle on the ground (y = 0), w across x and l along z, its texture once over it; two triangles. `split`:
// each triangle with its own copies of the shared corners (two pieces by index, one by position).
DecodedBatch rectangle(float w, float l, uint32_t colour, bool split = false) {
    DecodedBatch b;
    b.vertices = {vertex(0, 0, 0, 0, 0, colour), vertex(w, 0, 0, 1, 0, colour), vertex(w, 0, l, 1, 1, colour),
                  vertex(0, 0, l, 0, 1, colour)};
    if (split) {
        b.vertices.push_back(vertex(0, 0, 0, 0, 0, colour));
        b.vertices.push_back(vertex(w, 0, l, 1, 1, colour));
        b.triangles = {0, 1, 2, 4, 5, 3};
    } else {
        b.triangles = {0, 1, 2, 0, 2, 3};
    }
    return b;
}

gt4::scene::TextureSummary summary(uint16_t size, float centreToRim) {
    gt4::scene::TextureSummary s;
    s.width = s.height = size;
    s.centreToRim = centreToRim;
    s.known = 1;
    return s;
}

constexpr float kIdentity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
constexpr uint32_t kPoolColour = 0x807C7C7Cu;   // (124, 124, 124), alpha 0x80: the 256 layer's vertex colour

}  // namespace

int main() {
    using gt4::scene::litRoadVerdict;
    using gt4::scene::measureLitRoad;

    // A lamp pool: 4 m by 10 m, a 128 x 128 texture over it (0.03 and 0.08 m per texel), a soft texture (c/r 1).
    const gt4::scene::TextureSummary soft = summary(128, 1.0f);
    const auto pool = measureLitRoad(rectangle(4, 10, kPoolColour), kIdentity, &soft);
    expect(std::fabs(pool.vertexLuminance - 124.0f) < 0.5f && pool.vertexAlpha == 128.0f, "the pool's vertex colour");
    expect(pool.upShare > 0.999f, "the pool faces up");
    expect(std::fabs(pool.width - 2.0f * 40.0f / 28.0f) < 1e-3f, "the pool's width is 2 x area / perimeter");
    expect(std::fabs(pool.texel - 10.0f / 128.0f) < 1e-4f, "the pool's texel is the larger of the two axes'");
    expect(litRoadVerdict(pool), "a lamp pool is lamp light");

    // The same pool in two pieces by index, one by position: welded, the same width.
    const auto welded = measureLitRoad(rectangle(4, 10, kPoolColour, true), kIdentity, &soft);
    expect(std::fabs(welded.width - pool.width) < 1e-3f, "pieces that share positions are one piece");

    // Moved 100 m away: the same measures.
    float moved[4][4];
    std::memcpy(moved, kIdentity, sizeof moved);
    moved[3][0] = 100.0f;
    moved[3][2] = -50.0f;
    const auto far = measureLitRoad(rectangle(4, 10, kPoolColour), moved, &soft);
    expect(std::fabs(far.width - pool.width) < 1e-3f && far.upShare > 0.999f && litRoadVerdict(far), "a moved pool is the same pool");

    // On its side (turned 90 degrees about x: y -> z): a wall, not up-facing.
    const float onSide[4][4] = {{1, 0, 0, 0}, {0, 0, 1, 0}, {0, -1, 0, 0}, {0, 0, 0, 1}};
    const auto wall = measureLitRoad(rectangle(4, 10, kPoolColour), onSide, &soft);
    expect(wall.upShare < 0.001f && !litRoadVerdict(wall), "a wall is not lamp light");

    // A road marking: 0.15 m wide, 10 m long: 2 x 1.5 / 20.3 = 0.15 m, under 0.9.
    const auto marking = measureLitRoad(rectangle(0.15f, 10, kPoolColour), kIdentity, &soft);
    expect(marking.width < 0.2f && !litRoadVerdict(marking), "a road marking is not lamp light (too narrow)");

    // The checkers: 2 m square under a 256 texture: 0.0078 m per texel, under 0.03.
    const gt4::scene::TextureSummary fine = summary(256, 1.0f);
    const auto checkers = measureLitRoad(rectangle(2, 2, kPoolColour), kIdentity, &fine);
    expect(checkers.texel < 0.01f && !litRoadVerdict(checkers), "the checkers are not lamp light (too fine a texel)");

    // A dim item (vertex colour luminance 10) and a translucent one (alpha 64).
    expect(!litRoadVerdict(measureLitRoad(rectangle(4, 10, 0x800A0A0Au), kIdentity, &soft)), "a dim item is not lamp light");
    expect(!litRoadVerdict(measureLitRoad(rectangle(4, 10, 0x407C7C7Cu), kIdentity, &soft)), "a faint item is not lamp light");

    // A glow texture (c/r 5), and no texture summary at all.
    const gt4::scene::TextureSummary glow = summary(128, 5.0f);
    expect(!litRoadVerdict(measureLitRoad(rectangle(4, 10, kPoolColour), kIdentity, &glow)), "a glow is not lamp light");
    const auto unknown = measureLitRoad(rectangle(4, 10, kPoolColour), kIdentity, nullptr);
    expect(unknown.centreToRim < 0.0f && unknown.texel == 0.0f && !litRoadVerdict(unknown), "no summary, no verdict");

    // The layer's draw state, and three near misses.
    gt4::gfx::ModelJob job{};
    job.chain = 5u << 4;   // no lighting chain, body 0
    gt4::gfx::Context context{};
    context.blendA = 0;
    context.blendB = 1;
    context.blendC = 0;
    context.blendD = 1;
    context.depthTest = 1;
    context.depthFunc = 2;   // GEQUAL
    context.depthWrite = 0;
    gt4::scene::ItemSource item;
    item.batch.job = &job;
    item.context = &context;
    item.prim = (1u << 4) | (1u << 6);   // textured, alpha-blended
    expect(gt4::scene::litRoadState(item), "the layer's state");
    context.depthWrite = 1;
    expect(!gt4::scene::litRoadState(item), "with z write: not the layer");
    context.depthWrite = 0;
    job.chain |= 3u << 16;
    expect(!gt4::scene::litRoadState(item), "with a lighting body: not the layer");
    job.chain = 5u << 4;
    item.prim = 1u << 4;
    expect(!gt4::scene::litRoadState(item), "without alpha blending: not the layer");

    if (failures) return 1;
    std::printf("lit_road: all checks passed\n");
    return 0;
}
