// B7's parity planning and B8's mesh blocks (platform/render/modern/parity_view.h): a synthetic list whose view has one
// instance of two items in one Draw, its mesh decoded by the mesh cache: the spans name the items' vertex ranges and
// their mesh's place in a block, the staged upload holds the mesh's raw vertices and triangles in the GS path's order
// (rotated when the device has no last provoking vertex), unused meshes retire, and what cannot be drawn falls back by
// reason. And the block ranges' allocator.
#include "parity_view.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace gt4;

int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// One strip of four vertices (two triangles), integer positions and ST (0x1810); `shift` moves it (another batch).
scene::BatchCopy batch(int32_t shift) {
    scene::BatchCopy b;
    b.job.count = b.job.nloop = 4;
    b.job.chain = 1u | 2u << 4 | 0xB48u << 16;
    b.job.prim = 0x14;
    b.job.renderer = 1;
    const int32_t xyz[4][3] = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {10, 10, -5}};
    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i < 3; ++i) b.words[k * 4 + i] = static_cast<uint32_t>(xyz[k][i] + shift);
        b.words[(64 + k) * 4] = static_cast<uint32_t>(k * 4096);
    }
    b.words[64 * 4 + 3] = 1;
    b.words[65 * 4 + 3] = (4 - 2) * 3;
    return b;
}

}  // namespace

int main() {
    auto cache = std::make_shared<scene::SceneCache>(1);   // (the front end's, as FrameList::sceneCache carries it)
    const std::vector<scene::BatchCopy> items = {batch(0), batch(100)};
    cache->meshes().request(items);
    cache->meshes().wait();
    const scene::MeshId id = cache->meshes().request(items);
    const std::shared_ptr<const scene::Mesh> mesh = cache->meshes().mesh(id);
    check(id && mesh && mesh->items.size() == 2 && mesh->indices.size() == 12, "the mesh: two items of two triangles");
    if (!mesh) return 1;

    // (FrameList holds a unique_ptr: made anew for each case)
    auto makeList = [&](std::shared_ptr<const scene::SceneCache> scenes) {
        gsn::FrameList l;
        l.models.resize(3);
        l.draws.resize(2);
        l.sceneCache = std::move(scenes);
        l.displayed = true;   // (a list cut for a game read is not: the read switch's case below)
        auto view = std::make_shared<scene::SceneView>();
        view->instances.resize(1);
        gsn::FrameList::ViewOnList v;
        v.package = view;
        v.ids = {scene::InstanceIds{id, 0, 2}};
        v.items = {{1, 1, 6, 6}, {2, 1, 12, 6}};   // jobs 1 and 2 in draw 1, after another job's 6 vertices
        l.views.push_back(v);
        return l;
    };
    gsn::FrameList list = makeList(cache);

    // One plan: the mesh placed in the first block, its upload staged, the spans pointing at it.
    const uint64_t vertexBytes = mesh->raw.size() * sizeof(scene::MeshRawVertex);
    const uint64_t indexAt = (vertexBytes + 15) / 16 * 16;
    render::ParityView parity(1 << 16, 2, 2, 1);
    std::vector<render::ParitySpan> spans;
    std::vector<uint8_t> staging;
    std::vector<render::ParityCopy> copies;
    parity.plan(list, spans, staging, copies);
    check(spans.size() == 2 && copies.size() == 1 && staging.size() == indexAt + 12 * 4, "both items planned, the mesh staged once");
    if (spans.size() == 2 && copies.size() == 1) {
        check(spans[0].draw == 1 && spans[0].first == 6 && spans[0].count == 6 && spans[0].job == 1 && spans[0].renderer == 1 &&
                  spans[0].block == 0 && spans[0].vertexOffset == copies[0].to && spans[0].indexOffset == copies[0].to + indexAt &&
                  spans[0].firstIndex == 0 && spans[0].indexCount == 6,
              "the first item: its job's vertices, its mesh's offsets in the block, its triangles first");
        check(spans[1].first == 12 && spans[1].job == 2 && spans[1].firstIndex == 6, "the second item after it");
        check(copies[0].from == 0 && copies[0].bytes == staging.size() && copies[0].block == 0, "one copy: the staged mesh");
    }
    const uint32_t* indices = reinterpret_cast<const uint32_t*>(staging.data() + indexAt);
    // The GS path's vertex order: the strips' odd triangles turned back.
    bool order = staging.size() >= indexAt + 48;
    for (size_t k = 0; order && k < 4; ++k) {
        const uint32_t* m = mesh->indices.data() + k * 3;
        const bool turnedBack = mesh->turned[k] != 0;
        order = indices[k * 3] == (turnedBack ? m[1] : m[0]) && indices[k * 3 + 1] == (turnedBack ? m[0] : m[1]) &&
                indices[k * 3 + 2] == m[2];
    }
    check(mesh->turned.size() == 4 && !mesh->turned[0] && mesh->turned[1] && !mesh->turned[2] && mesh->turned[3],
          "the mesh: each item's second triangle turned");
    check(order, "the triangles in the GS path's order, the turned ones turned back");
    check(staging.size() >= vertexBytes && std::memcmp(staging.data(), mesh->raw.data(), vertexBytes) == 0,
          "the raw vertices as the mesh has them");
    staging.clear();
    copies.clear();
    parity.plan(list, spans, staging, copies);
    check(spans.size() == 2 && copies.empty() && parity.stats().meshesPlaced == 1 && parity.stats().drawn == 4 &&
              parity.stats().items == 4,
          "planned again: the mesh on the GPU already, nothing staged");
    // Retired after its unused frames (2 here), its range free again one frame in flight later.
    for (int f = 0; f < 5; ++f) parity.endFrame();
    check(parity.stats().meshesRetired == 1 && parity.bytesUsed() == 0, "unused: retired, then its range freed");
    parity.plan(list, spans, staging, copies);
    check(copies.size() == 1 && parity.stats().meshesPlaced == 2, "drawn again: uploaded again");

    // Rotated triangles (no last provoking vertex): the strip's last vertex first, the same winding.
    {
        render::ParityView rotated;
        rotated.setRotate(true);
        std::vector<uint8_t> st;
        std::vector<render::ParityCopy> cp;
        rotated.plan(list, spans, st, cp);
        const uint32_t* r = reinterpret_cast<const uint32_t*>(st.data() + indexAt);
        check(st.size() >= indexAt + 24 && r[0] == mesh->indices[2] && r[1] == mesh->indices[0] && r[2] == mesh->indices[1] &&
                  r[3] == mesh->indices[5] && r[4] == mesh->indices[4] && r[5] == mesh->indices[3],
              "rotated: (a, b, v) becomes (v, a, b), after a turned one is turned back");
    }

    // Fallbacks by reason.
    {
        std::vector<uint8_t> st;
        std::vector<render::ParityCopy> cp;
        render::ParityView tiny(64, 1, 2, 1);   // (a block too small for the mesh)
        tiny.plan(list, spans, st, cp);
        check(spans.empty() && cp.empty() && tiny.stats().overBudget == 2, "over the budget: its items on the GS path");
        render::ParityView small;
        gsn::FrameList none = makeList(cache);
        none.views[0].ids[0].mesh = 0;
        small.plan(none, spans, st, cp);
        check(spans.empty() && small.stats().meshNone == 2, "mesh 0: its items on the GS path");
        none.views[0].ids.clear();
        none.views[0].items.clear();
        small.plan(none, spans, st, cp);
        check(spans.empty() && small.stats().viewsNotSubstituted == 1, "a cut view: wholly on the GS path");
        for (const uint8_t role : {uint8_t(gt4::gfx::scene::kViewHud), uint8_t(gt4::gfx::scene::kViewMenu),
                                   uint8_t(gt4::gfx::scene::kViewEnvCopy)}) {
            gsn::FrameList kept = makeList(cache);
            auto view = std::make_shared<scene::SceneView>(*kept.views[0].package);
            view->view.role = role;
            kept.views[0].package = view;
            small.plan(kept, spans, st, cp);
            check(spans.empty(), "a HUD, menu or env-copy view: never substituted");
        }
        check(small.stats().viewsOfRoleKept == 3, "... and counted");
        for (const uint16_t flag : {uint16_t(gt4::gfx::scene::kInstLightMap), uint16_t(1024) /* kInstReflection */}) {
            gsn::FrameList out = makeList(cache);
            auto view = std::make_shared<scene::SceneView>(*out.views[0].package);
            view->instances[0].flags = flag;
            out.views[0].package = view;
            render::ParityView fresh;
            fresh.plan(out, spans, st, cp);
            check(spans.empty() && fresh.stats().itemsOutOfBinding == 2 && fresh.stats().drawn == 0,
                  "a light-map or road-reflection instance: its items on the GS path, counted");
        }
        gsn::FrameList differ = makeList(cache);
        differ.views[0].items[1].modelVertexCount = 3;
        small.plan(differ, spans, st, cp);
        check(spans.size() == 1 && small.stats().itemsDiffer == 1, "an item whose runs differ: not drawn here");
        // B8 step 4, the read switch: a list cut for a read, or an asynchronous read of the view's target: the GS path
        gsn::FrameList cut = makeList(cache);
        cut.displayed = false;
        small.plan(cut, spans, st, cp);
        check(spans.empty() && small.stats().viewsForReadSync == 1, "a list cut for a game read: its views on the GS path");
        gsn::FrameList async = makeList(cache);
        async.views[0].target = 7;
        async.readbacks.push_back({});
        async.readbacks[0].request.target = 7;
        small.plan(async, spans, st, cp);
        check(spans.empty() && small.stats().viewsForReadAsync == 1, "an asynchronous read of the view's target: the GS path");
        async.readbacks[0].request.target = 8;
        small.plan(async, spans, st, cp);
        check(spans.size() == 2, "a read of another target: the view substituted");
        async.displayed = false;   // (an asynchronous read cuts its list too: still only the named target's views)
        small.plan(async, spans, st, cp);
        check(spans.size() == 2 && small.stats().viewsForReadSync == 1, "cut for an asynchronous read of another target: substituted");
        async.readbacks[0].request.target = 7;
        small.plan(async, spans, st, cp);
        check(spans.empty() && small.stats().viewsForReadAsync == 2, "cut for an asynchronous read of the view's target: the GS path");
        small.setReadSwitch(false);
        small.plan(cut, spans, st, cp);
        check(spans.size() == 2, "the switch off (R9's checks): the read sees the substituted picture");
        gsn::FrameList gone = makeList(std::make_shared<scene::SceneCache>(1));   // (another cache: the id names nothing there)
        render::ParityView fresh;
        fresh.plan(gone, spans, st, cp);
        check(spans.empty() && fresh.stats().meshGone == 2, "a mesh the cache no longer has");
    }

    // The block ranges: first fit, alignment, a block added on demand, neighbours merged on release.
    {
        render::BlockRanges ranges(1024, 2);
        render::BlockRanges::Range a, b, c, d;
        check(ranges.allocate(100, 16, a) && a.block == 0 && a.offset == 0, "ranges: the first at 0");
        check(ranges.allocate(100, 16, b) && b.block == 0 && b.offset == 112, "ranges: the next aligned");
        check(ranges.allocate(900, 16, c) && c.block == 1 && c.offset == 0, "ranges: too big for the rest: a new block");
        check(!ranges.allocate(1000, 16, d), "ranges: no third block");
        check(!ranges.allocate(2000, 16, d), "ranges: larger than a block");
        ranges.release(a);
        ranges.release(b);
        check(ranges.allocate(1024, 16, d) && d.block == 0 && d.offset == 0, "ranges: released neighbours merged into the whole block");
        check(ranges.used() == 1024 + 900, "ranges: the bytes in use");
    }

    if (failures) {
        std::fprintf(stderr, "parity_view: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("parity_view: ok\n");
    return 0;
}
