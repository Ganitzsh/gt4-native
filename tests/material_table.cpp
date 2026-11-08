// The material table (platform/scene/material_table.h, ticket B6): the same key gets the same id across frames, other
// keys other ids, an unused key is dropped after its frames and comes back under a new id, and the counters say so.
#include "material_table.h"

#include <cstdio>

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

    std::printf("material_table: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
