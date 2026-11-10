// PLAN.md C12's round trip (platform/gs/gsdump_writer): a synthetic frame drawn by the native renderer's front end
// from its GIF packets, and the same frame written as a PCSX2 GS dump and read back through GsFrontend::gif (no
// GPU): the starting state (registers and GS memory) carried by the dump's state, the frame's packets by its transfer
// packets. The two frame lists must be equal: every draw's state, every vertex, every texture decoded, every target.
// The state's own encoding round trips byte for byte, and the file has PCSX2's layout (header, packets).
#include "gsdump_writer.h"
#include "gs_frontend.h"
#include "gs_memory.h"
#include "gs_packets.h"
#include "temp_path.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace gt4::test;
using gt4::gs::GsDump;
using gt4::gs::GsDumpState;
using gt4::gs::GsDumpWriter;
using gt4::gsn::FrameList;
using gt4::gsn::GsFrontend;
using gt4::gsn::GsMemory;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

constexpr uint32_t kTexBp = 0x1000;   // the texture's blocks (CT32, 64 x 32, TBW 1)

// The frame's starting state, as GIF writes (what setup() and a texture's registers would set) and as the dump's
// state: the same values both ways.
constexpr uint64_t kFrame = frame(0, 10, PSMCT32), kZbuf = zbuf(0x70, PSMZ24, false),
                   kScissor = scissor(0, 639, 0, 447), kOffset = xyoffset(2048 << 4, 2048 << 4),
                   kTest = test(true, 2), kTex0 = tex0(kTexBp, 1, PSMCT32, 6, 5, true, 1), kTex1 = 0x60,
                   kAlpha = 0x8000000044ull, kTexa = uint64_t(0x80) << 32, kFogcol = 0x191919;

Gif startAsGif() {
    Gif g;
    g.ad({{FRAME_1, kFrame}, {ZBUF_1, kZbuf}, {SCISSOR_1, kScissor}, {XYOFFSET_1, kOffset}, {TEST_1, kTest},
          {TEX0_1, kTex0}, {TEX1_1, kTex1}, {ALPHA_1, kAlpha}, {TEXA, kTexa}, {0x3D /* FOGCOL */, kFogcol},
          {PRMODECONT, 1}, {DTHE, 0}, {COLCLAMP, 1}, {PABE, 0}});
    return g;
}

GsDumpState startAsState(const GsMemory& memory) {
    GsDumpState s;
    auto& c = s.context[0];
    c.frame = kFrame;
    c.zbuf = kZbuf;
    c.scissor = kScissor;
    c.xyoffset = kOffset;
    c.test = kTest;
    c.tex0 = kTex0;
    c.tex1 = kTex1;
    c.alpha = kAlpha;
    s.texa = kTexa;
    s.fogcol = kFogcol;
    s.prmodecont = 1;
    s.colclamp = 1;
    s.memory.assign(memory.data(), memory.data() + GsMemory::kBytes);
    return s;
}

// The frame: an upload into GS memory, an untextured sprite, a textured blended sprite sampling the texture the
// starting memory holds, a Gouraud triangle strip with Z, a second texture set and sampled, and a FINISH.
Gif framePackets() {
    Gif g;
    std::vector<uint32_t> texels(16 * 16);
    for (uint32_t i = 0; i < texels.size(); ++i) texels[i] = 0x80000000u | (i * 0x010203u);
    g.upload(0x1800, 1, PSMCT32, 16, 16, texels.data(), texels.size() * 4);
    sprite(g, 10, 20, 200, 120, rgbaq(200, 30, 40, 0x80));
    g.reglist(prim(6, false, true, true, true), {RGBAQ, UV, XYZ2, UV, XYZ2},
              {rgbaq(0x80, 0x80, 0x80, 0x80), uv(0, 0), xyz(300, 40), uv(64, 32), xyz(428, 104)});
    g.reglist(prim(4, true, false, false, false), {RGBAQ, XYZF2},
              {rgbaq(255, 0, 0, 0x80), xyz(50, 300, 0x1000) | (uint64_t(7) << 56), rgbaq(0, 255, 0, 0x80),
               xyz(150, 280, 0x2000), rgbaq(0, 0, 255, 0x80), xyz(60, 400, 0x3000), rgbaq(255, 255, 0, 0x80),
               xyz(170, 390, 0x4000)});
    g.ad({{TEX0_1, tex0(0x1800, 1, PSMCT32, 4, 4, true, 0)}}, false);
    g.reglist(prim(6, false, true, false, true), {UV, XYZ2, UV, XYZ2},
              {uv(0, 0), xyz(500, 300), uv(16, 16), xyz(564, 364)});
    g.ad({{FINISH, 0}});
    return g;
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
            x.target != y.target || x.depth != y.depth || x.texture.kind != y.texture.kind ||
            x.texture.id != y.texture.id || x.texture.version != y.texture.version ||
            x.texture.offsetX != y.texture.offsetX || x.texture.offsetY != y.texture.offsetY)
            return false;
    }
    return true;
}

bool sameTextures(const FrameList& a, const FrameList& b) {
    if (a.textures.size() != b.textures.size()) return false;
    for (size_t i = 0; i < a.textures.size(); ++i) {
        const auto &x = a.textures[i], &y = b.textures[i];
        if (x.id != y.id || x.version != y.version || x.width != y.width || x.height != y.height ||
            x.pixels != y.pixels)
            return false;
    }
    return true;
}

bool sameTargets(const FrameList& a, const FrameList& b) {
    if (a.targets.size() != b.targets.size()) return false;
    for (size_t i = 0; i < a.targets.size(); ++i) {
        const auto &x = a.targets[i], &y = b.targets[i];
        if (x.id != y.id || x.bp != y.bp || x.bw != y.bw || x.psm != y.psm || x.height != y.height ||
            x.depth != y.depth)
            return false;
    }
    return true;
}

}  // namespace

int main() {
    // The starting GS memory: a 64 x 32 CT32 texture at kTexBp, and a pattern below the frame.
    auto memory = std::make_unique<GsMemory>();
    for (uint32_t y = 0; y < 32; ++y)
        for (uint32_t x = 0; x < 64; ++x)
            memory->writePixel(PSMCT32, kTexBp, 1, x, y, 0x80000000u | (x * 4) | ((y * 8) << 8) | (0x40u << 16));
    for (uint32_t x = 0; x < 640; x += 3) memory->writePixel(PSMCT32, 0, 10, x, 447, 0x80123456u);

    std::vector<uint8_t> privileged(GsDumpState::kPrivilegedBytes, 0);
    const std::pair<uint32_t, uint64_t> display[] = {{0x0000, pmode(true, false)}, {0x0020, smode2(true, false)},
                                                     {0x0070, dispfb(0, 10, PSMCT32)}, {0x0080, display640x448()}};
    for (const auto& [offset, value] : display) std::memcpy(&privileged[offset], &value, 8);

    // The state's encoding: version 9, 4 MiB + 509 bytes, the registers where PCSX2 keeps them, decoded back the same.
    {
        const GsDumpState s = startAsState(*memory);
        const std::vector<uint8_t> bytes = s.encode();
        expect(bytes.size() == 4194813, "the state is 4 MiB + 509 bytes");
        uint32_t version;
        uint64_t frameAt, scissorAt;
        std::memcpy(&version, bytes.data(), 4);
        std::memcpy(&frameAt, bytes.data() + 204, 8);     // context 1's FRAME (PCSX2's dump: byte 204)
        std::memcpy(&scissorAt, bytes.data() + 172, 8);   // context 1's SCISSOR (byte 172)
        expect(version == 9 && frameAt == kFrame && scissorAt == kScissor, "registers at PCSX2's offsets");
        expect(!std::memcmp(bytes.data() + 425, memory->data(), GsMemory::kBytes), "GS memory at byte 425");
        GsDumpState back;
        std::string error;
        expect(back.decode(bytes.data(), bytes.size(), error) && back.encode() == bytes, "the state decodes back");
        expect(back.context[0] == s.context[0] && back.context[1] == s.context[1] && back.texa == s.texa,
               "the decoded registers");
        std::vector<uint8_t> wrong = bytes;
        wrong[0] = 8;
        expect(!back.decode(wrong.data(), wrong.size(), error), "a version 8 state is refused");
        expect(!back.decode(bytes.data(), bytes.size() - 1, error), "a short state is refused");
    }

    // A: the frame drawn from its packets, with the starting registers set by GIF.
    const Gif start = startAsGif(), packets = framePackets();
    GsFrontend a;
    a.restoreMemory(memory->data());
    a.gif(3, static_cast<const uint8_t*>(start.data()), start.bytes());
    a.gif(3, static_cast<const uint8_t*>(packets.data()), packets.bytes());
    FrameList listA;
    a.vsync(1, listA);

    // B: the dump: the starting state, the frame's packets, a VSync; read back and drawn.
    const auto path = gt4::test::uniqueTempPath("gt4_gsdump", ".gs");
    {
        GsDumpWriter w;
        std::string error;
        expect(w.open(path.string(), startAsState(*memory), privileged.data(), error), "the dump opens");
        w.transfer(3, static_cast<const uint8_t*>(packets.data()), packets.bytes());
        w.vsync(0, privileged.data());
        expect(w.close(error), "the dump is written");
    }
    // The file's layout: PCSX2's header, the state, the registers, then a transfer, the registers and a VSync.
    {
        std::ifstream in(path, std::ios::binary);
        const std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t header = 8 + 36 + 10;
        const size_t expected = header + 4194813 + 8192 + (6 + packets.bytes()) + (1 + 8192) + 2;
        uint32_t words[11];
        std::memcpy(words, f.data(), sizeof words);
        expect(f.size() == expected, "the file's size: header, state, registers, three packets");
        expect(words[0] == 0xFFFFFFFFu && words[1] == 36 + 10 && words[2] == 9 && words[3] == 4194813 &&
                   words[4] == 36 && words[5] == 10 && words[6] == 0x77E61C8A,
               "PCSX2's header (version 9, the state's size, the serial at 36)");
        expect(f.size() > header && !std::memcmp(f.data() + 8 + 36, "SCUS-97328", 10), "the serial");
        expect(f[header + 4194813 + 8192] == 0 && f[header + 4194813 + 8192 + 1] == 3, "a path 3 transfer first");
    }
    GsDump dump;
    std::string error;
    expect(dump.read(path.string(), error), "the dump reads back");
    std::filesystem::remove(path);
    expect(dump.serial == "SCUS-97328" && dump.crc == 0x77E61C8A && dump.privileged == privileged,
           "the serial, CRC and registers read back");
    expect(dump.packets.size() == 3 && dump.packets[0].id == 0 && dump.packets[1].id == 3 && dump.packets[2].id == 1,
           "a transfer, the registers, a VSync");

    GsFrontend b;
    b.restoreMemory(dump.state.memory.data());
    const std::vector<uint64_t> registers = dump.state.registersAsGif();
    b.gif(3, reinterpret_cast<const uint8_t*>(registers.data()), registers.size() * 8);
    FrameList listB;
    for (const GsDump::Packet& p : dump.packets) {
        if (p.id == 0) b.gif(p.path, p.data.data(), p.data.size());
        if (p.id == 1) b.vsync(1, listB);
    }

    // The control: the same packets without the dump's registers draw another frame (the state is what carries them).
    GsFrontend c;
    c.restoreMemory(dump.state.memory.data());
    FrameList listC;
    for (const GsDump::Packet& p : dump.packets) {
        if (p.id == 0) c.gif(p.path, p.data.data(), p.data.size());
        if (p.id == 1) c.vsync(1, listC);
    }
    expect(!sameDraws(listA, listC), "without the state's registers the draws differ (the comparison can fail)");

    expect(!listA.draws.empty() && listA.draws.size() >= 4, "the frame has its draws");
    expect(sameDraws(listA, listB), "every draw's state, range, target and texture equal");
    expect(sameVertices(listA, listB), "every vertex equal");
    expect(sameTextures(listA, listB), "every texture decoded equal");
    expect(sameTargets(listA, listB), "every target equal");
    expect(!std::memcmp(a.memory().data(), b.memory().data(), GsMemory::kBytes), "GS memory equal after the frame");
    std::printf("gsdump: %zu draws, %zu vertices, %zu textures, %zu targets; %s\n", listA.draws.size(),
                listA.vertices.size(), listA.textures.size(), listA.targets.size(), failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
