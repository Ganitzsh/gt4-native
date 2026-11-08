// The material table (platform/scene/material_table.h, ticket B6): the same key gets the same id across frames, other
// keys other ids, an unused key is dropped after its frames and comes back under a new id, and the counters say so.
// And the scene cache's per-frame instances: a billboard run (kInstBillboard) gets its materials and no mesh.
#include "material_table.h"
#include "scene_cache.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

gt4::scene::MaterialKey key(uint64_t texture, uint32_t state) {
    gt4::scene::MaterialKey k;
    k.texture = texture;
    k.state = state;
    k.body = 3;
    k.dot = 0x121;
    return k;
}

}  // namespace

int main() {
    using gt4::scene::MaterialId;
    gt4::scene::MaterialTable table;
    table.setUnusedFrames(2);

    const MaterialId a = table.id(key(1, 10)), b = table.id(key(2, 10)), c = table.id(key(1, 11));
    expect(a != 0 && b != 0 && c != 0, "ids are never 0");
    expect(a != b && a != c && b != c, "different keys, different ids");
    expect(table.id(key(1, 10)) == a, "the same key, the same id in a frame");

    gt4::scene::MaterialKey back;
    expect(table.key(b, back) && back.texture == 2 && back.state == 10, "an id gives its key back");
    expect(!table.key(0, back) && !table.key(99, back), "unknown ids give nothing");

    // Across frames: a asked for every frame stays; b not asked for goes after two frames.
    for (int f = 0; f < 3; ++f) {
        table.endFrame();
        expect(table.id(key(1, 10)) == a, "the same key, the same id across frames");
    }
    expect(!table.key(b, back), "a key unused for its frames is dropped");
    const MaterialId b2 = table.id(key(2, 10));
    expect(b2 != 0 && b2 != b && b2 != a, "a dropped key asked for again gets a new id");

    // Class bits: given when an id is made, kept with it; the classifier is not asked again for a key it has seen.
    int asked = 0;
    const MaterialId d = table.id(key(9, 10), [&] { ++asked; return 0x4u; });
    expect(table.classes(d) == 0x4u, "a new id keeps its class bits");
    expect(table.id(key(9, 10), [&] { ++asked; return 0x8u; }) == d && table.classes(d) == 0x4u && asked == 1,
           "the classifier runs only for a new key");
    expect(table.classes(a) == 0 && table.classes(0) == 0, "no classifier, or no id: no class bits");

    const gt4::scene::MaterialTable::Stats s = table.stats();
    expect(s.created == 5, "created counts the new ids (a, b, c, b again, d)");
    expect(s.dropped == 2, "dropped counts b and c");
    expect(s.held == 3, "held: a, b again and d");
    expect(s.requests == 10, "requests counts every call");

    // B6 for A7 step 1b: a billboard run's quads are rebuilt every frame, so no mesh is asked for (no key hashed, no
    // decode queued), mesh 0 counted as per-frame; its material is made as any other (the glow classifier needs it).
    {
        gt4::scene::SceneCache cache(1);
        gt4::gfx::ModelJob job{};
        job.count = job.nloop = 4;
        std::vector<uint32_t> words(gt4::gfx::kModelWords, 0);
        gt4::gfx::Context context{};
        gt4::scene::ItemSource item;
        item.batch = {&job, words.data()};
        item.prim = 0x14;
        item.context = &context;
        item.texture = 0x1234;
        gt4::scene::InstanceData billboard;
        billboard.flags = gt4::gfx::scene::kInstBillboard;
        std::vector<MaterialId> materials;
        gt4::scene::SceneCounters counters;
        const gt4::scene::InstanceIds ids = cache.assign(billboard, &item, 1, materials, counters);
        expect(ids.mesh == 0 && counters.meshesPerFrame == 1 && counters.meshesNew == 0 && cache.meshes().stats().requests == 0,
               "a billboard run: mesh 0, per-frame, no mesh asked for");
        expect(ids.materialCount == 1 && materials.size() == 1 && materials[0] != 0 && counters.materialsNew == 1,
               "a billboard run: its material made");
        for (const uint16_t flag : {uint16_t(gt4::gfx::scene::kInstLightMap), uint16_t(gt4::gfx::scene::kInstReflection)}) {
            gt4::scene::InstanceData out;
            out.flags = flag;
            counters = {};
            materials.clear();
            const gt4::scene::InstanceIds o = cache.assign(out, &item, 1, materials, counters);
            expect(o.mesh == 0 && counters.meshesOutOfBinding == 1 && counters.meshesNew == 0 && o.materialCount == 1 &&
                       cache.meshes().stats().requests == 0,
                   "a light map or the road reflection: mesh 0, no mesh asked for, its material made");
        }
        gt4::scene::InstanceData course;   // (the same item without the flag: a mesh asked for)
        counters = {};
        cache.assign(course, &item, 1, materials, counters);
        expect(counters.meshesNew == 1 && cache.meshes().stats().requests == 1, "without the flag: a mesh asked for");
    }

    std::printf("material_table: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
