// F2b's glow class and lamp pass (PLAN.md F2b) on synthetic data: a texture summary of a glow and of a flat texture;
// the street-lamp draw's signature; the classifier's bit and colour; the lamp pass on a SceneFrame with two instances
// of one mesh (one extraction, then a cache hit), their transforms, a non-glow item, a missing mesh; and the gather's
// host records without kFromGame.
#include "course_lamp_pass.h"
#include "glow_classifier.h"
#include "gt4/render/light_list.h"
#include "lights_gather.h"
#include "texture_summary.h"

#include <cmath>
#include <cstdio>
#include <memory>
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

scene::Mesh lampMesh() {   // item 0: two lamps (quads) at (0, 5, 0) and (10, 5, 0); item 1: a quad that is not a glow
    scene::Mesh mesh;
    auto quad = [&](float x, float y, float h) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        const float c[4][2] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
        for (const auto& p : c) {
            scene::MeshVertex v;
            v.position[0] = x + p[0];
            v.position[1] = y + p[1];
            v.colour = 0x80804080u;   // R 0x80 (1.0), G 0x40 (0.5), B 0x80 (1.0)
            mesh.vertices.push_back(v);
        }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
    };
    quad(0.0f, 5.0f, 0.5f);
    quad(10.0f, 5.0f, 0.5f);
    scene::MeshItem a;
    a.firstIndex = 0;
    a.indexCount = 12;
    mesh.items.push_back(a);
    quad(50.0f, 0.0f, 2.0f);
    scene::MeshItem b;
    b.firstIndex = 12;
    b.indexCount = 6;
    mesh.items.push_back(b);
    mesh.key.packets = 0x1234;
    return mesh;
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

    // 4. The lamp pass on a SceneFrame: view 1, two instances of mesh 7 (item 0 a glow, item 1 not), one of mesh 9 whose
    // mesh is gone.
    auto mesh = std::make_shared<const scene::Mesh>(lampMesh());
    scene::SceneFrame sf;
    scene::ViewData v;
    v.id = 1;
    v.firstInstance = 0;
    v.instanceCount = 3;
    sf.views.push_back(v);
    scene::InstanceData a;
    a.view = 1;
    a.mesh = 7;
    a.firstMaterial = 0;
    a.materialCount = 2;
    a.objectToView.m[3][2] = -10.0f;   // 10 units ahead
    scene::InstanceData b = a;
    b.object = 2;
    b.axisScale[0] = b.axisScale[1] = b.axisScale[2] = 2.0f;   // twice the size, 20 to the left
    b.objectToView.m[3][0] = -20.0f;
    scene::InstanceData gone = a;
    gone.object = 3;
    gone.mesh = 9;
    sf.instances = {a, b, gone};
    sf.materials = {100, 101};   // all three share materials 100 (glow) and 101
    LampSources sources;
    sources.mesh = [&](scene::MeshId id) { return id == 7 ? mesh : nullptr; };
    const uint8_t mean[3] = {255, 128, 64};   // (1, 0.5, 0.25)
    sources.classes = [&](scene::MaterialId id) { return id == 100 ? glowClasses(mean) : 0u; };
    CourseLampPass pass;
    std::vector<gfx::scene::Light> lights;
    CourseLampPass::Stats s;
    pass.lights(sf, 1, sources, lights, &s);
    expect(s.instances == 3 && s.glowItems == 3 && s.lamps == 4, "two instances' two lamps; the gone mesh's none");
    expect(s.extracted == 1 && s.cacheHits == 1 && s.meshMissing == 1, "one extraction, a cache hit, a missing mesh counted");
    expect(lights.size() == 4, "four lights");
    if (lights.size() == 4) {
        const gfx::scene::Light& l = lights[0];
        expect(l.view == 1 && l.source == gfx::scene::kSrcCourseGlow && l.kind == gfx::scene::kLightPoint, "a course glow point light");
        expect(near(l.position[0], 0.0f) && near(l.position[1], 5.0f) && near(l.position[2], -10.0f), "placed by objectToView");
        expect(near(l.width, 0.5f * std::sqrt(2.0f)) && near(l.height, l.width), "half the quad's diagonal");
        expect(near(l.colour[0], 1.0f) && near(l.colour[1], 128.0f / 255.0f * 0.5f) && near(l.colour[2], 64.0f / 255.0f), "texture colour x vertex colour");
        const gfx::scene::Light& big = lights[3];   // instance b's second lamp: (10, 5, 0) x 2 + (-20, 0, -10)
        expect(near(big.position[0], 0.0f) && near(big.position[1], 10.0f) && near(big.position[2], -10.0f) &&
                   near(big.width, std::sqrt(2.0f)),
               "the axis scale scales the position and the radius");
    }
    // the next frame: everything from the caches
    lights.clear();
    pass.lights(sf, 1, sources, lights, &s);
    expect(s.extracted == 0 && s.cacheHits == 2 && s.lamps == 4, "the next frame extracts nothing");
    pass.lights(sf, 2, sources, lights, &s);
    expect(s.lamps == 0, "another view: none");

    // 5. The gather: the game's records keep kFromGame, the host's lamps don't.
    std::vector<gfx::scene::Light> records;
    gfx::scene::Light game{};
    game.view = 1;
    game.kind = gfx::scene::kLightPoint;
    game.source = gfx::scene::kSrcWarning;
    game.position[2] = -5.0f;
    game.colour[0] = 1.0f;
    game.intensity = 1.0f;
    records.push_back(game);
    records.insert(records.end(), lights.begin(), lights.end());
    std::vector<GpuLight> out;
    GatherStats gs;
    gatherLights(records.data(), records.size(), 1, 0.5f, 500.0f, 1.0f, out, gs, 1);
    size_t fromGame = 0, host = 0;
    for (const GpuLight& g : out) (g.flags & kFromGame ? fromGame : host) += 1;
    expect(out.size() == 5 && fromGame == 1 && host == 4, "the host's course lamps without kFromGame");

    std::printf("course_lamp_pass: %s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
