// F2b's glow class and lamp pass (PLAN.md F2b) on synthetic data: a texture summary of a glow and of a flat texture;
// the street-lamp draw's signature; the classifier's bit and colour; the lamp pass on a SceneFrame's billboard
// candidates (A7 step 1b): a glow promoted with its colour, a tree left, a candidate without an instance, one merged with
// a game record (entries2[]), another view's left alone; and the gather: promoted lamps lit, candidates never.
#include "course_lamp_pass.h"
#include "glow_classifier.h"
#include "gt4/render/light_list.h"
#include "lights_gather.h"
#include "texture_summary.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace gt4;
using namespace gt4::render;

namespace {

int failures = 0;
void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what.c_str());
        ++failures;
    }
}
bool near(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }

// A w x w texture: a glow (bright centre, alpha 0x80 in its core, falling to dark and transparent at the rim), or flat.
std::vector<uint32_t> texture(uint32_t w, bool glow) {
    std::vector<uint32_t> t(size_t(w) * w);
    for (uint32_t y = 0; y < w; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const float dx = (x + 0.5f - 0.5f * w) / (0.5f * w), dy = (y + 0.5f - 0.5f * w) / (0.5f * w);
            const float d = std::sqrt(dx * dx + dy * dy);
            const float f = glow ? std::max(0.0f, 1.0f - d) : 0.6f;
            const uint32_t r = uint32_t(255 * f), g = uint32_t(200 * f), b = uint32_t(120 * f);
            const uint32_t a = glow ? (d < 0.2f ? 0x80u : uint32_t(0x7F * f)) : 0x80u;
            t[size_t(y) * w + x] = r | (g << 8) | (b << 16) | (a << 24);
        }
    return t;
}

gfx::scene::Light light(uint16_t view, uint8_t source, uint32_t id, float x, float y, float z, float half) {
    gfx::scene::Light l{};
    l.view = view;
    l.kind = gfx::scene::kLightPoint;
    l.source = source;
    l.id = id;
    l.position[0] = x;
    l.position[1] = y;
    l.position[2] = z;
    l.width = l.height = half;
    l.colour[0] = l.colour[1] = l.colour[2] = 1.0f;
    l.intensity = 1.0f;
    return l;
}

}  // namespace

int main() {
    // 1. The texture summary and its glow check.
    const std::vector<uint32_t> glowTex = texture(32, true), flatTex = texture(32, false);
    const scene::TextureSummary glowSum = scene::summarizeTexture(glowTex.data(), 32, 32);
    const scene::TextureSummary flatSum = scene::summarizeTexture(flatTex.data(), 32, 32);
    expect(glowSum.known && glowSum.width == 32 && glowSum.centreToRim > 3.0f && glowSum.alphaCore > 0, "a glow: bright centre, an opaque core");
    expect(glowTexture(glowSum).glow, "a glow texture passes");
    expect(flatSum.known && near(flatSum.centreToRim, 1.0f, 0.05f) && !glowTexture(flatSum).glow, "a flat texture does not");
    expect(!glowTexture(scene::TextureSummary{}).glow, "no summary: not a glow");
    scene::TextureSummary noCore = glowSum;   // the street-lamp billboards' glow: no texel at alpha 0x80
    noCore.alphaCore = 0;
    expect(glowTexture(noCore).glow, "a radial glow without an opaque core is a glow");
    const std::vector<uint32_t> bigTex = texture(512, true);   // sampled every 4th texel each way
    const scene::TextureSummary bigSum = scene::summarizeTexture(bigTex.data(), 512, 512);
    expect(bigSum.width == 512 && glowTexture(bigSum).glow && near(bigSum.centreToRim, glowSum.centreToRim, 0.2f * glowSum.centreToRim),
           "a large glow, sampled on a stride, scores as the small one");

    // 2. The signature.
    gfx::Context c;
    c.blendA = 0; c.blendB = 1; c.blendC = 0; c.blendD = 1;
    c.alphaTest = 1; c.alphaFunc = 4; c.alphaRef = 0x80;
    c.depthWrite = 1;
    gfx::ModelJob job{};
    job.chain = 0;   // body 0
    uint32_t words[gfx::kModelWords] = {};
    scene::ItemSource item;
    item.batch = {&job, words};
    item.prim = (1u << 4) | (1u << 6);   // textured, blended
    item.context = &c;
    item.texture = 0xABCDEF;
    expect(glowSignature(item), "the street-lamp draw's signature");
    gfx::Context other = c;
    other.alphaFunc = 6;   // GREATER
    scene::ItemSource o = item;
    o.context = &other;
    expect(!glowSignature(o), "another alpha test: not the signature");
    gfx::ModelJob lit = job;
    lit.chain = 3u << 16;
    o = item;
    o.batch = {&lit, words};
    expect(!glowSignature(o), "a lit batch: not the signature");
    o = item;
    o.texture = 0;
    expect(!glowSignature(o), "untextured: not the signature");

    // 3. The classifier: the bit with the colour in it, one verdict per texture.
    GlowClassifier classifier;
    const uint32_t bits = classifier.classify(item, &glowSum);
    float colour[3] = {};
    glowColour(bits, colour);
    expect((bits & kMaterialGlow) && near(colour[0], glowSum.mean[0] / 255.0f, 0.5f / 255.0f) &&
               near(colour[2], glowSum.mean[2] / 255.0f, 0.5f / 255.0f),
           "a glow material gets the bit, its colour in the bits");
    expect(classifier.classify(item, &flatSum) == bits, "the same texture keeps its first verdict");
    scene::ItemSource flatItem = item;
    flatItem.texture = 0x1111;
    expect(classifier.classify(flatItem, &flatSum) == 0, "a flat texture: no bit");
    expect(classifier.classify(o, &glowSum) == 0, "the signature fails first");
    expect(classifier.classify(item, nullptr) == 0, "no summary: no bit");
    const GlowClassifier::Stats cs = classifier.stats();
    expect(cs.judged == 5 && cs.signature == 4 && cs.glows == 2 && cs.noSummary == 1 && cs.textures == 2, "the classifier's counts");

    // 4. The lamp pass on a SceneFrame: two kInstBillboard instances, serial 5 (material 100, a glow) and serial 6
    // (material 101, a tree); candidates naming them, one naming no instance, one at a game kSrcCourseGlow record, one
    // in another view.
    using namespace gfx::scene;
    scene::SceneFrame sf;
    scene::InstanceData lamps, trees;
    lamps.serial = 5;
    lamps.view = 1;
    lamps.firstMaterial = 0;
    lamps.materialCount = 1;
    trees = lamps;
    trees.serial = 6;
    trees.firstMaterial = 1;
    sf.instances = {lamps, trees};
    sf.materials = {100, 101};
    const uint8_t mean[3] = {255, 128, 64};   // the glow texture's mean (1, 0.5, 0.25)
    auto classes = [&](scene::MaterialId id) { return id == 100 ? glowClasses(mean) : 0u; };
    std::vector<Light> records;
    records.push_back(light(1, kSrcCourseGlow, 0x900, 10.0f, 5.0f, -10.0f, 0.0f));   // a game record (entries2[])
    records.push_back(light(1, kSrcBillboard, 5, 0.0f, 5.0f, -10.0f, 1.0f));         // a lamp
    records[1].colour[1] = 2.0f;                                                     // (its tint: a channel up to 2)
    records.push_back(light(1, kSrcBillboard, 6, 3.0f, 2.0f, -12.0f, 2.0f));         // a tree
    records.push_back(light(1, kSrcBillboard, 99, 4.0f, 5.0f, -10.0f, 1.0f));        // no such instance
    records.push_back(light(1, kSrcBillboard, 5, 10.3f, 5.0f, -10.0f, 0.5f));        // the game record's lamp
    records.push_back(light(2, kSrcBillboard, 5, 0.0f, 5.0f, -10.0f, 1.0f));         // another view
    records.push_back(light(1, kSrcBillboard, 5, 0.8f, 5.0f, -10.0f, 1.0f));         // a second head on the same post
    records.push_back(light(1, kSrcBillboard, 5, 9.8f, 5.0f, -10.0f, 0.5f));         // a second billboard at the game lamp
    CourseLampPass pass;
    CourseLampPass::Stats s;
    pass.promote(sf, 1, classes, records, &s);
    expect(s.candidates == 6 && s.kept == 2 && s.dropped == 1 && s.noInstance == 1 && s.merged == 2 && s.mergedAgain == 1,
           "the candidates' counts (a second billboard at one game lamp counted)");
    expect(records[1].source == kSrcCourseGlow && near(records[1].colour[0], 1.0f) &&
               near(records[1].colour[1], 2.0f * 128.0f / 255.0f) && near(records[1].colour[2], 64.0f / 255.0f),
           "a glow billboard: a course lamp, the texture's mean times its own colour");
    expect(records[2].source == kSrcBillboard && records[3].source == kSrcBillboard, "a tree and an unknown instance stay candidates");
    expect(records[4].source == kSrcBillboard && near(records[0].width, 0.5f) && near(records[0].position[0], 10.0f),
           "a billboard at a game record: one lamp, the record's position, the billboard's size");
    expect(records[5].source == kSrcBillboard, "another view's candidate left alone");
    expect(records[6].source == kSrcCourseGlow, "two lamp heads close together stay two lamps (no merge between candidates)");

    // 5. The gather: the promoted lamp and the game record light; the candidates never do.
    std::vector<GpuLight> out;
    GatherStats gs;
    gatherLights(records.data(), records.size(), 1, 0.5f, 500.0f, 1.0f, out, gs);
    expect(out.size() == 3 && gs.notLights == 4, "three lights (two lamps, the game record); four candidates not lit");

    std::printf("course_lamp_pass: %s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
