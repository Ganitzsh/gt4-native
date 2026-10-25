// The GS back end (paraLLEl-GS) checked by pixel with synthetic GIF / VIF streams: a sprite, alpha
// blending, a textured triangle, a CLUT (PSMT8) texture, a GS -> EE download, the FINISH/SIGNAL events,
// a VU1 microprogram (VIF1 MPG + UNPACK + MSCAL, transform + XGKICK) and a capture replayed through the
// host's graphics thread. Exits with 77 (skipped) when no Vulkan device meets paraLLEl-GS's requirements.
//
//   gt4_gs_backend_test [OUTPUT_DIR]           runs the checks; writes OUTPUT_DIR/replay.cap for the
//                                               gt4_host replay test (tests/host_replay.cmake)
//   gt4_gs_backend_test --check-ppm FILE        checks a gt4_host capture of that replay
#include "gs_backend.h"
#include "gs_host.h"
#include "gs_packets.h"
#include "link_host.h"
#include "stream.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace gt4::test;

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

uint32_t pixel(const gt4::gs::DisplayFrame& f, uint32_t x, uint32_t y) {
    return x < f.width && y < f.height ? f.rgba[size_t(y) * f.width + x] & 0xFFFFFF : 0xDEADBEEF;
}

void expectPixel(const gt4::gs::DisplayFrame& f, uint32_t x, uint32_t y, uint32_t rgb, const char* what) {
    const uint32_t got = pixel(f, x, y);
    // Allow +-1 per channel (CRTC readout rounding).
    bool ok = got != 0xDEADBEEF;
    for (int c = 0; c < 3 && ok; ++c) {
        const int a = int((got >> (8 * c)) & 0xFF), b = int((rgb >> (8 * c)) & 0xFF);
        ok = std::abs(a - b) <= 1;
    }
    if (!ok) std::fprintf(stderr, "  pixel (%u, %u) = %06X, expected %06X\n", x, y, got, rgb);
    expect(ok, what);
}

constexpr uint32_t rgb(uint32_t r, uint32_t g, uint32_t b) { return r | (g << 8) | (b << 16); }

// Context 1 draws into a 640 x 448 PSMCT32 frame at 0, no Z, whole-screen scissor.
Gif setup() {
    Gif g;
    g.ad({{FRAME_1, frame(0, 10, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, true)}, {SCISSOR_1, scissor(0, 639, 0, 447)},
          {XYOFFSET_1, xyoffset(2048 << 4, 2048 << 4)}, {TEST_1, test(true, 1)}, {PRMODECONT, 1}, {DTHE, 0},
          {COLCLAMP, 1}, {TEX1_1, 0}});
    return g;
}

void sprite(Gif& g, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint64_t color, bool abe = false) {
    g.reglist(prim(6, false, false, abe, false), {RGBAQ, XYZ2, XYZ2}, {color, xyz(x0, y0), xyz(x1, y1)});
}

// Privileged registers of a 640 x 448 interlaced display of the frame at 0. Interlaced with a
// full-height buffer read every other line per field (FFMD = 0), as paraLLEl-GS models it;
// force_progressive scans it out as one 640 x 448 frame.
const std::pair<uint32_t, uint64_t> kDisplay[] = {
    {0x00, pmode(true, false)}, {0x20, smode2(true, false)}, {0x70, dispfb(0, 10, PSMCT32)}, {0x80, display640x448()}, {0xE0, 0}};

void display(gt4::gs::Backend& gs) {
    for (const auto& [offset, value] : kDisplay) gs.writePrivileged(offset, value);
}

// The stream's GS for the VU1 test: the back end directly.
struct Target final : gt4::ps2::GsTarget {
    gt4::gs::Backend& gs;
    gt4::gs::DisplayFrame frame;
    explicit Target(gt4::gs::Backend& backend) : gs(backend) {}
    void gif(uint32_t path, const uint8_t* data, size_t bytes) override { gs.gif(path, data, bytes); }
    void privileged(uint32_t offset, uint64_t value) override { gs.writePrivileged(offset, value); }
    void vsync(uint32_t field, uint32_t count) override { gs.vsync(field, count, frame); }
    void readTransfer(void* out, uint32_t qwc) override { gs.readTransfer(out, qwc); }
};

// The replayed scene: a grey background, a blue sprite (PATH3) and a magenta triangle drawn by the VU1
// transform microprogram (PATH1), shown for two VBlanks.
const float kTriangle[3][2] = {{100, 60}, {340, 60}, {100, 300}};

void writeReplay(const std::string& path) {
    gt4::ps2::CaptureWriter writer(path);
    const uint32_t three = 3;
    Gif g = setup();
    sprite(g, 0, 0, 640, 448, rgbaq(64, 64, 64, 0x80));
    sprite(g, 400, 300, 600, 420, rgbaq(0, 0, 255, 0x80));
    for (int field = 0; field < 2; ++field) {
        writer.write(GT4_REC_GIF, &three, 4, g.data(), static_cast<uint32_t>(g.bytes()));
        const vif::Stream s = transformStream(kTriangle, 255, 0, 255);
        writer.write(GT4_REC_VIF1, s.bytes(), static_cast<uint32_t>(s.size()));
        for (const auto& [offset, value] : kDisplay) {
            struct { uint32_t reg, pad; uint64_t value; } priv{offset, 0, value};
            writer.write(GT4_REC_GS_PRIV, &priv, sizeof(priv));
        }
        const uint32_t vs[2] = {static_cast<uint32_t>(field), static_cast<uint32_t>(field)};
        writer.write(GT4_REC_VSYNC, vs, sizeof(vs));
    }
}

// Pixels of the replayed scene, in a 640 x 448 image.
struct Expected { uint32_t x, y, rgb; const char* what; };
const Expected kReplayPixels[] = {
    {20, 20, rgb(64, 64, 64), "replay background"},
    {150, 100, rgb(255, 0, 255), "replay VU1 triangle"},
    {500, 350, rgb(0, 0, 255), "replay PATH3 sprite"},
    {330, 290, rgb(64, 64, 64), "outside the triangle"},
};

int checkPpm(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    uint32_t width = 0, height = 0, maxValue = 0;
    in >> magic >> width >> height >> maxValue;
    in.get();
    if (!in || magic != "P6" || maxValue != 255) {
        std::fprintf(stderr, "check-ppm: %s is not a binary PPM\n", path.c_str());
        return 1;
    }
    std::vector<unsigned char> rgbBytes(size_t(width) * height * 3);
    in.read(reinterpret_cast<char*>(rgbBytes.data()), static_cast<std::streamsize>(rgbBytes.size()));
    // The capture may be larger than 640 x 448 (scaled): sample at the same relative positions.
    gt4::gs::DisplayFrame f;
    f.width = width;
    f.height = height;
    f.rgba.resize(size_t(width) * height);
    for (size_t i = 0; i < f.rgba.size(); ++i)
        f.rgba[i] = rgbBytes[i * 3] | (rgbBytes[i * 3 + 1] << 8) | (rgbBytes[i * 3 + 2] << 16);
    for (const auto& e : kReplayPixels)
        expectPixel(f, e.x * width / 640, e.y * height / 448, e.rgb, e.what);
    std::printf("check-ppm: %s %ux%u: %s\n", path.c_str(), width, height, failures ? "FAILED" : "matches");
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--check-ppm") return checkPpm(argv[2]);
    const std::string outputDir = argc > 1 ? argv[1] : "";
    std::string error;
    gt4::gs::BackendOptions options;
    if (const char* gpu = std::getenv("GT4_GS_GPU")) options.gpu = gpu;   // device index or name
    auto gs = gt4::gs::Backend::create(options, error);
    if (!gs) {
        std::printf("gs_backend: skipped, no suitable Vulkan device (%s)\n", error.c_str());
        return 77;
    }
    std::printf("gs_backend: paraLLEl-GS on %s\n", gs->deviceName().c_str());
    std::vector<std::pair<gt4::gs::GsEventKind, uint64_t>> events;
    gs->setEventSink([&](gt4::gs::GsEventKind kind, uint64_t payload) { events.emplace_back(kind, payload); });

    // 1. Sprites and alpha blending.
    {
        Gif g = setup();
        sprite(g, 0, 0, 640, 448, rgbaq(32, 64, 128, 0x80));
        sprite(g, 100, 100, 200, 150, rgbaq(255, 0, 0, 0x80));
        // (Cs - Cd) * As + Cd with As = 0x40 (one half): green over the background.
        g.ad({{ALPHA_1, 0x44}}, false);  // A = Cs (0), B = Cd (1), C = As (0), D = Cd (1)
        sprite(g, 300, 300, 340, 340, rgbaq(0, 255, 0, 0x40), true);
        g.ad({{FINISH, 0}, {SIGNAL, 0x1234ull | (0xFFFFull << 32)}});
        gs->gif(3, g.data(), g.bytes());
        display(*gs);
        gt4::gs::DisplayFrame f;
        expect(gs->vsync(0, 1, f), "vsync scans out a frame");
        std::printf("  frame %ux%u (mode %ux%u)\n", f.width, f.height, f.modeWidth, f.modeHeight);
        expect(f.width == 640 && f.height == 448, "frame is 640 x 448");
        expectPixel(f, 10, 10, rgb(32, 64, 128), "background sprite");
        expectPixel(f, 150, 120, rgb(255, 0, 0), "red sprite inside");
        expectPixel(f, 99, 120, rgb(32, 64, 128), "red sprite left edge is exclusive of x < 100");
        expectPixel(f, 200, 120, rgb(32, 64, 128), "red sprite right edge excludes x = 200");
        expectPixel(f, 320, 320, rgb(16, 64 + 95, 64), "alpha blend (Cs - Cd) * As + Cd");
        bool finish = false, signal = false;
        for (const auto& [kind, payload] : events) {
            finish |= kind == gt4::gs::GsEventKind::Finish;
            signal |= kind == gt4::gs::GsEventKind::Signal && (payload & 0xFFFF) == 0x1234;
        }
        expect(finish, "FINISH reported");
        expect(signal, "SIGNAL reported with its ID");
    }

    // 2. A textured triangle (PSMCT32, 16 x 16, four 8 x 8 quadrants), DECAL, nearest.
    {
        uint32_t texels[16 * 16];
        const uint32_t quadrant[4] = {rgb(255, 255, 0), rgb(0, 255, 255), rgb(255, 0, 255), rgb(255, 255, 255)};
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 16; ++x) texels[y * 16 + x] = quadrant[(x / 8) + 2 * (y / 8)] | 0x80000000u;
        Gif g = setup();
        sprite(g, 0, 0, 640, 448, rgbaq(0, 0, 0, 0x80));
        g.upload(8192, 1, PSMCT32, 16, 16, texels, sizeof(texels));
        g.ad({{TEXFLUSH, 0}, {TEX0_1, tex0(8192, 1, PSMCT32, 4, 4, true, 1)}, {CLAMP_1, 0}}, false);
        g.reglist(prim(3, false, true, false, true), {UV, XYZ2},
                  {uv(0, 0), xyz(300, 100), uv(16, 0), xyz(400, 100), uv(0, 16), xyz(300, 200)});
        gs->gif(3, g.data(), g.bytes());
        gt4::gs::DisplayFrame f;
        gs->vsync(1, 2, f);
        expectPixel(f, 310, 110, quadrant[0], "texture quadrant 0");
        expectPixel(f, 360, 105, quadrant[1], "texture quadrant 1");
        expectPixel(f, 305, 160, quadrant[2], "texture quadrant 2");
        expectPixel(f, 390, 190, rgb(0, 0, 0), "outside the triangle");
    }

    // 3. A CLUT texture: PSMT8 indices 0..3, a PSMCT32 palette (CSM1), drawn with a textured sprite.
    {
        uint8_t indices[16 * 16];
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 16; ++x) indices[y * 16 + x] = static_cast<uint8_t>((x / 8) + 2 * (y / 8));
        uint32_t palette[256] = {};
        palette[0] = rgb(200, 10, 10) | 0x80000000u;
        palette[1] = rgb(10, 200, 10) | 0x80000000u;
        palette[2] = rgb(10, 10, 200) | 0x80000000u;
        palette[3] = rgb(100, 100, 100) | 0x80000000u;
        Gif g = setup();
        sprite(g, 0, 0, 640, 448, rgbaq(0, 0, 0, 0x80));
        g.upload(12288, 1, PSMCT32, 16, 16, palette, sizeof(palette));
        g.upload(10240, 1, PSMT8, 16, 16, indices, sizeof(indices));
        g.ad({{TEXFLUSH, 0}, {TEX0_1, tex0(10240, 1, PSMT8, 4, 4, true, 1, 12288, PSMCT32, 0, 0, 1)}}, false);
        g.reglist(prim(6, false, true, false, true), {UV, XYZ2, UV, XYZ2},
                  {uv(0, 0), xyz(500, 300), uv(16, 16), xyz(564, 364)});
        gs->gif(3, g.data(), g.bytes());
        gt4::gs::DisplayFrame f;
        gs->vsync(0, 3, f);
        expectPixel(f, 510, 310, rgb(200, 10, 10), "CLUT index 0");
        expectPixel(f, 550, 310, rgb(10, 200, 10), "CLUT index 1");
        expectPixel(f, 510, 350, rgb(10, 10, 200), "CLUT index 2");
        expectPixel(f, 550, 350, rgb(100, 100, 100), "CLUT index 3");
    }

    // 4. GS -> EE: read back a 4 x 2 rectangle of the frame (the red sprite is gone: the frame was cleared
    // in test 3, so read the CLUT sprite's corner).
    {
        Gif g;
        g.ad({{BITBLTBUF, bitbltbuf(0, 0, 0, 0, 10, PSMCT32)}, {TRXPOS, trxpos(0, 0, 500, 300)}, {TRXREG, trxreg(4, 2)},
              {TRXDIR, 1}});
        gs->gif(3, g.data(), g.bytes());
        uint32_t words[8] = {};
        gs->readTransfer(words, 2);
        bool ok = true;
        for (uint32_t w : words) ok &= (w & 0xFFFFFF) == rgb(200, 10, 10);
        if (!ok) std::fprintf(stderr, "  read %08X %08X ...\n", words[0], words[1]);
        expect(ok, "GS -> EE transfer returns the frame's pixels");
    }

    // 5. A VU1 microprogram: VIF1 uploads it (MPG) and its data (UNPACK), MSCAL runs it; it transforms
    // three vertices and XGKICKs a triangle (PATH1).
    {
        Target target(*gs);
        gt4::ps2::GraphicsStream stream(target);
        Gif g = setup();
        sprite(g, 0, 0, 640, 448, rgbaq(0, 0, 0, 0x80));
        gs->gif(3, g.data(), g.bytes());
        const vif::Stream s = transformStream(kTriangle, 255, 0, 255);
        stream.record({GT4_REC_VIF1, static_cast<uint32_t>(s.size()), 1}, s.bytes());
        gt4::gs::DisplayFrame f;
        gs->vsync(0, 5, f);
        expectPixel(f, 150, 100, rgb(255, 0, 255), "VU1-transformed triangle (inside)");
        expectPixel(f, 330, 290, rgb(0, 0, 0), "VU1-transformed triangle (outside)");
        expect(stream.vifStats().microprograms == 1, "one VU1 microprogram ran");
    }

    // 6. The replay hook: a capture of the same kind of stream, played by the host's graphics thread.
    {
        const std::filesystem::path dir = outputDir.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(outputDir);
        std::filesystem::create_directories(dir);
        const std::string capture = (dir / "replay.cap").string();
        writeReplay(capture);
        gs.reset();   // one GS device at a time
        gt4::gs::GsHost::Options hostOptions;
        hostOptions.replayPath = capture;
        hostOptions.lockstep = true;
        hostOptions.backend = options;
        auto host = gt4::gs::GsHost::create(hostOptions, error);
        expect(host != nullptr, "GsHost opens the replay");
        if (host) {
            int frames = 0;
            while (host->advance()) ++frames;
            gt4::gs::DisplayFrame f;
            uint64_t serial = 0;
            expect(host->takeFrame(f, serial) && serial == 2, "two VBlanks replayed, the last frame displayed");
            expect(f.width == 640 && f.height == 448, "replayed frame is 640 x 448");
            for (const auto& p : kReplayPixels) expectPixel(f, p.x, p.y, p.rgb, p.what);
            expect(host->finished(), "the replay ends");
        }
        if (outputDir.empty()) std::filesystem::remove(capture);
    }

    if (failures) {
        std::fprintf(stderr, "gs_backend: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("gs_backend: all checks passed\n");
    return 0;
}
