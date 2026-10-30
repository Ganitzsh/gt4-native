// The graphics host without a GPU: the native renderer's front end reports SIGNAL / FINISH / LABEL, serves
// GS -> host reads from GS memory and asks for the GPU when the pixels were drawn, and a capture replayed
// through GsHost gives one frame list per VBlank. Also writes the capture the gt4_host replay test draws
// (tests/host_replay.cmake) and checks that test's image.
//
//   gt4_gs_host_test [OUTPUT_DIR]       runs the checks; writes OUTPUT_DIR/replay.cap
//   gt4_gs_host_test --check-ppm FILE    checks a gt4_host capture of that replay
#include "gs_frontend.h"
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

// Pixels of the replayed scene, in a 640 x 448 image. The test's microprogram is not one of GT4's: only the
// VU1 interpreter (GT4_VU1_INTERPRETER builds) runs it.
struct Expected { uint32_t x, y, rgb; const char* what; };
const Expected kReplayPixels[] = {
    {20, 20, rgb(64, 64, 64), "replay background"},
#if GT4_VU1_INTERPRETER
    {150, 100, rgb(255, 0, 255), "replay VU1 triangle"},
#else
    {150, 100, rgb(64, 64, 64), "no VU1 triangle without the interpreter"},
#endif
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
    std::vector<unsigned char> bytes(size_t(width) * height * 3);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    // The capture may be larger than 640 x 448 (scaled): sample at the same relative positions; +-1 per channel.
    for (const auto& e : kReplayPixels) {
        const size_t at = (size_t(e.y * height / 448) * width + e.x * width / 640) * 3;
        bool ok = at + 2 < bytes.size();
        for (int c = 0; c < 3 && ok; ++c) ok = std::abs(int(bytes[at + c]) - int((e.rgb >> (8 * c)) & 0xFF)) <= 1;
        if (!ok && at + 2 < bytes.size())
            std::fprintf(stderr, "  pixel (%u, %u) = %02X%02X%02X, expected %06X\n", e.x, e.y, bytes[at + 2], bytes[at + 1], bytes[at], e.rgb);
        expect(ok, e.what);
    }
    std::printf("check-ppm: %s %ux%u: %s\n", path.c_str(), width, height, failures ? "FAILED" : "matches");
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--check-ppm") return checkPpm(argv[2]);
    const std::string outputDir = argc > 1 ? argv[1] : "";

    // 1. Events: FINISH, SIGNAL (with its ID and mask) and LABEL as a packet writes them.
    {
        gt4::gsn::GsFrontend fe;
        std::vector<std::pair<uint32_t, uint64_t>> events;
        fe.setEventSink([&](uint32_t kind, uint64_t value) { events.emplace_back(kind, value); });
        Gif g = setup();
        sprite(g, 0, 0, 640, 448, rgbaq(32, 64, 128, 0x80));
        g.ad({{FINISH, 0}, {SIGNAL, 0x1234ull | (0xFFFFull << 32)}, {LABEL, 0x55ull | (0xFFull << 32)}});
        fe.gif(3, static_cast<const uint8_t*>(g.data()), g.bytes());
        expect(events.size() == 3, "three events");
        expect(events.size() == 3 && events[0].first == 0, "FINISH reported");
        expect(events.size() == 3 && events[1].first == 1 && events[1].second == (0x1234ull | (0xFFFFull << 32)), "SIGNAL reported with its ID");
        expect(events.size() == 3 && events[2].first == 2 && (events[2].second & 0xFF) == 0x55, "LABEL reported with its ID");
    }

    // 2. GS -> host reads: uploaded pixels come from GS memory; drawn ones are asked of the GPU.
    {
        gt4::gsn::GsFrontend fe;
        uint32_t texels[4 * 2];
        for (uint32_t i = 0; i < 8; ++i) texels[i] = 0x80000000u | (i * 0x010203u);
        Gif g = setup();
        g.upload(8192, 1, PSMCT32, 4, 2, texels, sizeof(texels));
        g.ad({{BITBLTBUF, bitbltbuf(0, 0, 0, 8192, 1, PSMCT32)}, {TRXPOS, trxpos(0, 0, 0, 0)}, {TRXREG, trxreg(4, 2)}, {TRXDIR, 1}});
        fe.gif(3, static_cast<const uint8_t*>(g.data()), g.bytes());
        gt4::gsn::ReadRequest request;
        expect(!fe.takeGpuRead(request) && !fe.readFromTarget(), "an uploaded rectangle is read from memory");
        uint32_t words[8] = {};
        fe.readTransfer(words, 2);
        bool same = true;
        for (uint32_t i = 0; i < 8; ++i) same &= words[i] == texels[i];
        expect(same, "the read returns the uploaded pixels");

        Gif d = setup();
        sprite(d, 0, 0, 640, 448, rgbaq(200, 10, 10, 0x80));
        d.ad({{BITBLTBUF, bitbltbuf(0, 0, 0, 0, 10, PSMCT32)}, {TRXPOS, trxpos(0, 0, 500, 300)}, {TRXREG, trxreg(4, 2)}, {TRXDIR, 1}});
        fe.gif(3, static_cast<const uint8_t*>(d.data()), d.bytes());
        expect(fe.readFromTarget(), "a drawn rectangle is a target's");
        expect(fe.takeGpuRead(request) && request.x == 500 && request.y == 300 && request.width == 4 && request.height == 2 &&
                   request.psm == PSMCT32 && request.target != 0,
               "its pixels are asked of the GPU");
        gt4::gsn::FrameList partial;
        fe.cut(partial);
        expect(!partial.displayed && partial.draws.size() == 1, "the list cut at the read holds the draw before it");
        std::vector<uint8_t> pixels(4 * 2 * 4, 0xAB);
        fe.setReadData(pixels);
        fe.readTransfer(words, 2);
        expect(words[0] == 0xABABABABu && words[7] == 0xABABABABu, "the GPU's pixels are what the read returns");
    }

    // 3. A capture replayed through GsHost (no game: reads stay in memory): one displayed list per VBlank.
    {
        const std::filesystem::path dir = outputDir.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(outputDir);
        std::filesystem::create_directories(dir);
        const std::string capture = (dir / "replay.cap").string();
        writeReplay(capture);
        gt4::gs::GsHost::Options options;
        options.replayPath = capture;
        options.lockstep = true;
        std::string error;
        auto host = gt4::gs::GsHost::create(options, error);
        expect(host != nullptr, "GsHost opens the replay");
        if (host) {
            while (host->advance()) {}
            std::vector<gt4::gsn::FrameList> lists;
            expect(host->takeNativeFrames(lists), "frame lists were made");
            size_t displayed = 0, draws = 0;
            for (const auto& l : lists) {
                displayed += l.displayed ? 1 : 0;
                draws += l.draws.size();
                if (l.displayed) expect(l.display.target1 != 0, "the display shows a drawn target");
            }
            expect(displayed == 2, "two VBlanks, two displayed frame lists");
            expect(draws >= 2, "each field's sprites drawn (one batch: they share their state)");
            expect(host->finished(), "the replay ends");
        }
        if (outputDir.empty()) std::filesystem::remove(capture);
    }

    if (failures) {
        std::fprintf(stderr, "gs_host: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("gs_host: all checks passed\n");
    return 0;
}
