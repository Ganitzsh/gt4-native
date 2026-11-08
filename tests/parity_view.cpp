// B7's parity planning (platform/render/modern/parity_view.h): a synthetic list whose view has one instance of two
// items in one Draw, its mesh decoded by the mesh cache: the spans name the items' vertex ranges and arena places, the
// arena holds the mesh's raw vertices and triangles in the GS path's order (rotated when the
// device has no last provoking vertex), and what cannot be drawn falls back by reason.
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

    std::vector<scene::MeshRawVertex> vertices(64);
    std::vector<uint32_t> indices(64);
    render::ParityView parity;
    parity.setArena(vertices.data(), 64, indices.data(), 64, false);
    std::vector<render::ParitySpan> spans;
    check(parity.plan(list, spans) && spans.size() == 2, "both items planned");
    if (spans.size() == 2) {
        check(spans[0].draw == 1 && spans[0].first == 6 && spans[0].count == 6 && spans[0].job == 1 && spans[0].renderer == 1 &&
                  spans[0].firstIndex == 0 && spans[0].indexCount == 6 && spans[0].vertexOffset == 0,
              "the first item: its job's vertices, its triangles at the arena's start");
        check(spans[1].first == 12 && spans[1].job == 2 && spans[1].firstIndex == 6, "the second item after it");
    }
    // The GS path's vertex order: the strips' odd triangles turned back.
    bool order = true;
    for (size_t k = 0; k < 4; ++k) {
        const uint32_t* m = mesh->indices.data() + k * 3;
        const bool turnedBack = mesh->turned[k] != 0;
        order = order && indices[k * 3] == (turnedBack ? m[1] : m[0]) && indices[k * 3 + 1] == (turnedBack ? m[0] : m[1]) &&
                indices[k * 3 + 2] == m[2];
    }
    check(mesh->turned.size() == 4 && !mesh->turned[0] && mesh->turned[1] && !mesh->turned[2] && mesh->turned[3],
          "the mesh: each item's second triangle turned");
    check(order, "the triangles in the GS path's order, the turned ones turned back");
    check(std::memcmp(vertices.data(), mesh->raw.data(), mesh->raw.size() * sizeof(scene::MeshRawVertex)) == 0,
          "the raw vertices as the mesh has them");
    check(parity.plan(list, spans) && parity.stats().meshesPlaced == 1 && parity.stats().drawn == 4 && parity.stats().items == 4,
          "planned again: the mesh placed once");

    // Rotated triangles (no last provoking vertex): the strip's last vertex first, the same winding.
    {
        render::ParityView rotated;
        rotated.setArena(vertices.data(), 64, indices.data(), 64, true);
        check(rotated.plan(list, spans), "rotated: planned");
        check(indices[0] == mesh->indices[2] && indices[1] == mesh->indices[0] && indices[2] == mesh->indices[1] &&
                  indices[3] == mesh->indices[5] && indices[4] == mesh->indices[4] && indices[5] == mesh->indices[3],
              "rotated: (a, b, v) becomes (v, a, b), after a turned one is turned back");
    }

    // Fallbacks by reason.
    {
        render::ParityView small;
        small.setArena(vertices.data(), 2, indices.data(), 64, false);
        check(!small.plan(list, spans), "an arena too small: the plan says so (the caller empties it)");
        small.setArena(vertices.data(), 64, indices.data(), 64, false);
        gsn::FrameList none = makeList(cache);
        none.views[0].ids[0].mesh = 0;
        check(small.plan(none, spans) && spans.empty() && small.stats().meshNone == 2, "mesh 0: its items on the GS path");
        none.views[0].ids.clear();
        none.views[0].items.clear();
        check(small.plan(none, spans) && spans.empty() && small.stats().viewsNotSubstituted == 1, "a cut view: wholly on the GS path");
        gsn::FrameList differ = makeList(cache);
        differ.views[0].items[1].modelVertexCount = 3;
        check(small.plan(differ, spans) && spans.size() == 1 && small.stats().itemsDiffer == 1, "an item whose runs differ: not drawn here");
        gsn::FrameList gone = makeList(std::make_shared<scene::SceneCache>(1));   // (another cache: the id names nothing there)
        render::ParityView fresh;
        fresh.setArena(vertices.data(), 64, indices.data(), 64, false);
        check(fresh.plan(gone, spans) && spans.empty() && fresh.stats().meshGone == 2, "a mesh the cache no longer has");
    }

    if (failures) {
        std::fprintf(stderr, "parity_view: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("parity_view: ok\n");
    return 0;
}
