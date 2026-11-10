// PLAN.md C12 step 3 (platform/gs/gsdump_encoder): the host's records as GIF in a PCSX2 GS dump. A front end takes a
// stream of records directly (A); the same records go through GsDumpEncoder, whose dump is read back and fed to
// another front end as GIF, after the dump's starting registers and memory (B). The dumped frame's lists must be equal:
// every draw's state, every vertex, every texture, every target, and GS memory after it. The frame before FROM carries
// only state records, so B's starting registers are the encoder's shadow of them: the shadow is what is tested there.
// The dumped frame: a state record over both contexts and the shared groups, an upload, a UV sprite, a fogged Gouraud
// strip split across two draw records (no PRIM between them: the strip goes on), an ST/Q-textured triangle with
// attributes-only vertices before it, the CPU renderers' primitives (GT4_GPU_MODELS=0's path) and a second texture.
#include "gs_frontend.h"
#include "gs_memory.h"
#include "gs_primitives.h"
#include "gsdump_encoder.h"
#include "gsdump_writer.h"
#include "temp_path.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using gt4::gs::GsDump;
using gt4::gs::GsDumpEncoder;
using gt4::gsn::FrameList;
using gt4::gsn::GsFrontend;
using gt4::gsn::GsMemory;
namespace gfx = gt4::gfx;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

constexpr uint16_t kOrigin = 2048 << 4;
gfx::Vertex vertex(uint32_t x, uint32_t y, uint32_t z, uint32_t rgba, uint8_t kick = 1, uint16_t u = 0, uint16_t v = 0, float s = 0,
                   float t = 0, float q = 1, uint8_t fog = 255) {
    gfx::Vertex out{};
    out.x = static_cast<uint16_t>(kOrigin + x * 16);
    out.y = static_cast<uint16_t>(kOrigin + y * 16);
    out.z = z;
    out.rgba = rgba;
    out.u = u;
    out.v = v;
    out.s = s;
    out.t = t;
    out.q = q;
    out.fog = fog;
    out.kick = kick;
    return out;
}

// The two streams: `a` takes the records, the encoder the same.
struct Both {
    GsFrontend& a;
    GsDumpEncoder& e;
    void state(const gfx::StateRecord& r) { e.state(r); a.applyState(r); }
    void upload(const gfx::UploadRecord& u, const std::vector<uint32_t>& pixels) {
        e.upload(u, reinterpret_cast<const uint8_t*>(pixels.data()), pixels.size() * 4);
        a.upload(u, reinterpret_cast<const uint8_t*>(pixels.data()), pixels.size() * 4);
    }
    void draw(uint16_t prim, uint32_t flags, const std::vector<gfx::Vertex>& v) {
        const gfx::DrawRecord d{prim, static_cast<uint16_t>(v.size()), flags};
        e.draw(d, v.data());
        a.draw(d, v.data());
    }
    void primitives(const gt4::ps2::GsPrimitives& p) {   // (as gs_host.cpp's native target hands them on)
        e.primitives(p);
        if (p.setPrim) a.setPrim(p.prim);
        std::vector<GsFrontend::InputVertex> run(p.count);
        for (size_t k = 0; k < p.count; ++k) {
            const gt4::ps2::GsVertex& v = p.vertices[k];
            run[k] = {v.s, v.t, v.q, v.rgba, v.x, v.y, v.z, v.fog, v.adc, {0, 0, 0}, {0, 0, 0, 0}};
        }
        a.vertices(run.data(), run.size(), p.fog);
    }
};

gfx::StateRecord startState() {   // frame 1's: what the dump's starting registers must carry
    gfx::StateRecord r{};
    gfx::Context& c = r.context;
    c.colorBase = 0;
    c.colorWidth = 10;
    c.colorFormat = gfx::C32;
    c.depthBase = 0x70 * 32;
    c.depthFormat = 0x31;   // Z24
    c.depthWrite = 1;
    c.originX = kOrigin;
    c.originY = kOrigin;
    c.scissorX0 = 0;
    c.scissorX1 = 639;
    c.scissorY0 = 0;
    c.scissorY1 = 447;
    c.blendA = 0;
    c.blendB = 1;
    c.blendC = 0;
    c.blendD = 1;
    c.blendFixed = 0x80;
    c.alphaTest = 1;
    c.alphaFunc = 6;
    c.alphaRef = 0x20;
    c.depthTest = 1;
    c.depthFunc = 2;
    c.sampler.wrapU = 1;
    c.sampler.wrapV = 2;
    c.sampler.minV = 3;
    c.sampler.maxV = 60;
    c.sampler.magLinear = 1;
    c.sampler.minFilter = 1;
    c.sampler.lodK = -24;
    c.texture.base = 0x1000;
    c.texture.width = 1;
    c.texture.format = gfx::C32;
    c.texture.log2Width = 6;
    c.texture.log2Height = 5;
    c.texture.useAlpha = 1;
    c.texture.function = 0;
    r.palette = c.texture;
    for (uint32_t g = 0; g < gfx::kContextGroups; ++g) r.contextGroups |= (1u << g) | (1u << (16 + g));
    r.shared.colorClamp = 1;
    r.shared.attributesFromPrim = 1;
    r.shared.fogColor = 0x191919;
    r.shared.texAlpha0 = 0x40;
    r.shared.texAlpha1 = 0x80;
    r.shared.ditherMatrix = 0x1234567;
    r.sharedGroups = (1u << gfx::kColorClamp) | (1u << gfx::kAttributes) | (1u << gfx::kFogColor) | (1u << gfx::kTexAlpha) |
                     (1u << gfx::kDitherMatrix);
    return r;
}

bool sameVertices(const FrameList& a, const FrameList& b) {
    if (a.vertices.size() != b.vertices.size()) return false;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        if (std::memcmp(&a.vertices[i], &b.vertices[i], sizeof a.vertices[i])) return false;
    return true;
}

bool sameDraws(const FrameList& a, const FrameList& b) {
    if (a.draws.size() != b.draws.size()) return false;
    for (size_t i = 0; i < a.draws.size(); ++i) {
        const auto &x = a.draws[i], &y = b.draws[i];
        if (!(x.state == y.state) || x.topology != y.topology || x.first != y.first || x.count != y.count ||
            x.target != y.target || x.depth != y.depth || x.texture.kind != y.texture.kind || x.texture.id != y.texture.id ||
            x.texture.offsetX != y.texture.offsetX || x.texture.offsetY != y.texture.offsetY)
            return false;
    }
    return true;
}

bool sameTextures(const FrameList& a, const FrameList& b) {
    if (a.textures.size() != b.textures.size()) return false;
    for (size_t i = 0; i < a.textures.size(); ++i) {
        const auto &x = a.textures[i], &y = b.textures[i];
        if (x.width != y.width || x.height != y.height || x.pixels != y.pixels) return false;
    }
    return true;
}

bool sameTargets(const FrameList& a, const FrameList& b) {
    if (a.targets.size() != b.targets.size()) return false;
    for (size_t i = 0; i < a.targets.size(); ++i) {
        const auto &x = a.targets[i], &y = b.targets[i];
        if (x.id != y.id || x.bp != y.bp || x.bw != y.bw || x.psm != y.psm || x.height != y.height || x.depth != y.depth)
            return false;
    }
    return true;
}

}  // namespace

int main() {
    // GS memory at the start: a 64 x 32 CT32 texture at 0x1000, the same for A and as the dump's start.
    auto memory = std::make_unique<GsMemory>();
    for (uint32_t y = 0; y < 32; ++y)
        for (uint32_t x = 0; x < 64; ++x)
            memory->writePixel(0, 0x1000, 1, x, y, 0x80000000u | (x * 4) | ((y * 8) << 8) | (0x40u << 16));

    GsDumpEncoder::Options o;
    expect(GsDumpEncoder::parse("dump.gs,1,1", o) && o.path == "dump.gs" && o.from == 1 && o.count == 1, "PATH,FROM,COUNT");
    expect(GsDumpEncoder::parse("x.gs", o) && o.from == 0 && o.count == 1, "FROM and COUNT optional");
    expect(!GsDumpEncoder::parse("", o) && !GsDumpEncoder::parse(nullptr, o), "an empty value is no dump");

    const auto path = gt4::test::uniqueTempPath("gt4_gsdump_encoder", ".gs");
    GsDumpEncoder::Options options;
    options.path = path.string();
    options.from = 1;
    options.count = 1;
    GsDumpEncoder e(options);
    GsFrontend a;
    a.restoreMemory(memory->data());
    Both both{a, e};

    // Frame 1, before FROM: state only (the shadow's test).
    both.state(startState());
    FrameList list1;
    e.vsync(0);
    expect(!e.writing(), "nothing written before FROM");
    a.vsync(1, list1);
    expect(e.opensAt(1) && !e.opensAt(2), "the dump opens at VBlank FROM");
    e.open(a.memory().data(), "the test's memory");
    expect(e.writing(), "the dump is open");

    // Frame 2, dumped.
    {
        gfx::StateRecord r{};   // context 2's blend, context 1's scissor, a shared group
        r.context = startState().context;
        r.palette = r.context.texture;
        r.context.scissorX1 = 319;
        r.context.blendFixed = 0x40;
        r.contextGroups = (1u << gfx::kScissor) | (1u << (16 + gfx::kBlendGroup));
        r.shared.fogColor = 0x203040;
        r.sharedGroups = 1u << gfx::kFogColor;
        both.state(r);
    }
    std::vector<uint32_t> texels(16 * 16);
    for (uint32_t i = 0; i < texels.size(); ++i) texels[i] = 0x80000000u | (i * 0x010203u);
    both.upload({0x1800, 1, gfx::C32, 0, 0, 16, 16, 0x100000, 64}, texels);
    // a UV sprite (the texture at 0x1000), blended
    both.draw(6 | gfx::kTextured | gfx::kBlend | gfx::kTexelCoords, gfx::kDrawPrim | gfx::kDrawColor | gfx::kDrawUv,
              {vertex(300, 40, 0, 0x80808080, 1, 0, 0), vertex(428, 104, 0, 0x80808080, 1, 64 * 16, 32 * 16)});
    // a fogged Gouraud strip split across two records: kick 0 starts, then more vertices with no PRIM between
    both.draw(4 | gfx::kGouraud | gfx::kFog, gfx::kDrawPrim | gfx::kDrawFog | gfx::kDrawColor,
              {vertex(50, 300, 0x1000, 0x800000FF, 0, 0, 0, 0, 0, 1, 7), vertex(150, 280, 0x2000, 0x8000FF00, 0, 0, 0, 0, 0, 1, 20),
               vertex(60, 400, 0x3000, 0x80FF0000, 1, 0, 0, 0, 0, 1, 40)});
    both.draw(0, gfx::kDrawFog | gfx::kDrawColor,
              {vertex(170, 390, 0x4000, 0x8000FFFF, 1, 0, 0, 0, 0, 1, 60), vertex(220, 320, 0x5000, 0x80FFFF00, 1, 0, 0, 0, 0, 1, 80)});
    // attributes only, then an ST/Q-textured triangle using them for its first vertex's colour
    both.draw(0, gfx::kDrawAttributesOnly | gfx::kDrawColor, {vertex(0, 0, 0, 0x80405060)});
    both.draw(3 | gfx::kTextured, gfx::kDrawPrim | gfx::kDrawSt,
              {vertex(400, 200, 0x100, 0, 1, 0, 0, 0.1f, 0.2f, 1.0f), vertex(500, 210, 0x100, 0, 1, 0, 0, 0.9f, 0.25f, 2.0f),
               vertex(450, 300, 0x100, 0, 1, 0, 0, 0.4f, 0.8f, 1.5f)});
    // the CPU renderers' primitives: a fogged textured strip with an ADC vertex
    {
        std::vector<gt4::ps2::GsVertex> v(4);
        for (uint32_t k = 0; k < 4; ++k) {
            v[k].s = 0.1f * float(k + 1);
            v[k].t = 0.05f * float(k + 2);
            v[k].q = 1.0f + 0.25f * float(k);
            v[k].rgba = 0x80000000u | (k * 0x203040u);
            v[k].x = kOrigin + (100 + 40 * k) * 16;
            v[k].y = kOrigin + (k & 1 ? 200 : 150) * 16;
            v[k].z = 0x8000 + k;
            v[k].fog = 10 * k;
            v[k].adc = k == 2;
        }
        gt4::ps2::GsPrimitives p;
        p.setPrim = true;
        p.prim = 4 | gfx::kGouraud | gfx::kTextured | gfx::kFog;
        p.fog = true;
        p.vertices = v.data();
        p.count = v.size();
        both.primitives(p);
    }
    // a second texture: TEX0 to the upload, a UV sprite
    {
        gfx::StateRecord r{};
        r.context = startState().context;
        r.context.texture.base = 0x1800;
        r.context.texture.log2Width = 4;
        r.context.texture.log2Height = 4;
        r.palette = r.context.texture;
        r.contextGroups = 1u << gfx::kTexture;
        both.state(r);
    }
    both.draw(6 | gfx::kTextured | gfx::kTexelCoords, gfx::kDrawPrim | gfx::kDrawColor | gfx::kDrawUv,
              {vertex(500, 300, 0, 0x80808080, 1, 0, 0), vertex(564, 364, 0, 0x80808080, 1, 16 * 16, 16 * 16)});
    FrameList listA;
    e.vsync(1);
    a.vsync(2, listA);
    expect(!e.writing(), "the dump closes after COUNT VSyncs");
    const std::string summary = e.summary();
    expect(summary.find("6 draws") != std::string::npos, "the summary counts the dumped draws");
    std::printf("%s\n", summary.c_str());

    // B: the dump read back, its starting registers and memory, its packets as GIF.
    GsDump dump;
    std::string error;
    expect(dump.read(path.string(), error), "the dump reads back");
    std::filesystem::remove(path);
    size_t transfers = 0, vsyncs = 0;
    for (const GsDump::Packet& p : dump.packets) {
        transfers += p.id == 0;
        vsyncs += p.id == 1;
        if (p.id == 0) expect(p.path == 3, "every transfer on path 3");
    }
    expect(vsyncs == 1 && transfers >= 8, "the frame's transfers and one VSync");
    GsFrontend b;
    b.restoreMemory(dump.state.memory.data());
    const std::vector<uint64_t> registers = dump.state.registersAsGif();
    b.gif(3, reinterpret_cast<const uint8_t*>(registers.data()), registers.size() * 8);
    FrameList listB;
    for (const GsDump::Packet& p : dump.packets) {
        if (p.id == 0) b.gif(p.path, p.data.data(), p.data.size());
        if (p.id == 1) b.vsync(2, listB);
    }
    expect(listA.draws.size() >= 4, "the frame has its draws (the front end merges same-state ones)");
    expect(sameDraws(listA, listB), "every draw's state, range, target and texture equal");
    expect(sameVertices(listA, listB), "every vertex equal");
    expect(sameTextures(listA, listB), "every texture decoded equal");
    expect(sameTargets(listA, listB), "every target equal");
    expect(!std::memcmp(a.memory().data(), b.memory().data(), GsMemory::kBytes), "GS memory equal after the frame");

    // The control: without the shadow's registers (frame 1's state) the frame draws differently.
    GsFrontend c;
    c.restoreMemory(dump.state.memory.data());
    FrameList listC;
    for (const GsDump::Packet& p : dump.packets) {
        if (p.id == 0) c.gif(p.path, p.data.data(), p.data.size());
        if (p.id == 1) c.vsync(2, listC);
    }
    expect(!sameDraws(listA, listC), "without the starting registers the draws differ (the comparison can fail)");

    std::printf("gsdump_encoder: %zu draws, %zu vertices, %zu textures, %zu targets; %s\n", listA.draws.size(),
                listA.vertices.size(), listA.textures.size(), listA.targets.size(), failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
