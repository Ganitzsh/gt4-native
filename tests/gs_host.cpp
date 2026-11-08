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
#include "temp_path.h"

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
        const gt4::gfx::DisplayRecord shown = displayRecord();
        writer.write(GT4_REC_DISPLAY, &shown, sizeof(shown));
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

    // 2b. Scene views (CONTRACTS.md a.5 7, a.8; A10's front-end half): two tagged 3D batches of two views, the same
    // state. Split off: one draw (the split's draws counted); split on: one per view, Draw::view set. The views bind to
    // the draws' target, an asynchronous read inside the frame cuts both, and the closed frame goes on the next list.
    for (const bool split : {false, true}) {
        gt4::gsn::GsFrontend fe;
        fe.setSceneSplit(split);
        Gif g = setup();
        fe.gif(3, static_cast<const uint8_t*>(g.data()), g.bytes());
        std::vector<uint32_t> words(gt4::gfx::kModelWords, 0);
        words[64 * 4 + 3] = 1;           // one strip
        words[65 * 4 + 3] = 3 * 3;       // of 3 + 2 = 5 vertices: 3 triangles
        gt4::gfx::ModelJob job{};
        job.count = 5;
        job.prim = 4;                    // triangle strip
        gt4::gfx::scene::FrameBegin begin{};
        begin.prefix = {gt4::gfx::scene::kFrameBegin, gt4::gfx::scene::kVersion};
        fe.sceneRecord(reinterpret_cast<const uint8_t*>(&begin), sizeof begin);
        job.pad[0] = 1;
        job.pad[1] = 1;
        fe.model(job, words.data());
        job.pad[0] = 2;
        job.pad[1] = 2;
        fe.model(job, words.data());
        gt4::gfx::scene::ViewEnd end1{};   // view 1 ends before the read: its package is on the list, it is not cut
        end1.prefix = {gt4::gfx::scene::kViewEnd, gt4::gfx::scene::kVersion};
        end1.view = 1;
        fe.sceneRecord(reinterpret_cast<const uint8_t*>(&end1), sizeof end1);
        {
            auto package = std::make_shared<gt4::scene::SceneView>();
            package->view.id = 1;
            fe.sceneView(std::move(package));
        }
        job.pad[0] = 0;                  // untagged: no view
        job.pad[1] = 0;
        fe.model(job, words.data());
        gt4::gsn::FrameList partial;
        fe.cut(partial, true);
        const std::string mode = split ? "split on: " : "split off: ";
        if (!split) {
            expect(partial.draws.size() == 1 && partial.draws[0].view == 0, (mode + "the views' batches merge into one draw").c_str());
            expect(fe.stats.sceneSplitDraws == 2, (mode + "the draws the split would add are counted").c_str());
        } else {
            expect(partial.draws.size() == 3 && partial.draws[0].view == 1 && partial.draws[1].view == 2 && partial.draws[2].view == 0,
                   (mode + "one draw per view, Draw::view set (0 untagged)").c_str());
            expect(fe.stats.sceneSplitDraws == 2, (mode + "the draws the split added are counted").c_str());
        }
        expect(!partial.scene, (mode + "a list cut before the FrameEnd has no scene frame").c_str());
        expect(partial.views.size() == 1 && partial.views[0].package && partial.views[0].package->view.id == 1,
               (mode + "the partial list carries the package of the view that ended before it").c_str());
        expect(partial.views.size() == 1 && partial.views[0].target != 0 && partial.views[0].target == partial.draws[0].target &&
                   !partial.views[0].splitTargets,
               (mode + "with the view's binding beside it").c_str());
        auto frame = std::make_unique<gt4::scene::SceneFrame>();
        for (uint16_t id = 1; id <= 3; ++id) {
            gt4::scene::ViewData v;
            v.id = id;
            frame->views.push_back(v);
        }
        fe.sceneFrame(std::move(frame));
        gt4::gsn::FrameList last;
        fe.vsync(1, last);
        expect(last.scene != nullptr, (mode + "the closed frame goes on the list that received its FrameEnd").c_str());
        if (last.scene) {
            const auto& v = last.scene->views;
            const auto& c = last.scene->counters;
            expect(v[0].target != 0 && v[0].target == partial.draws[0].target && v[1].target == v[0].target,
                   (mode + "each view bound to its first tagged draw's target").c_str());
            expect(!v[0].cut && v[1].cut && !v[2].cut && c.viewsCut == 1,
                   (mode + "a view still open at the read is cut; one ended before it is not").c_str());
            expect(c.cuts == 1 && c.cutsAsync == 1, (mode + "the asynchronous read's cut counted by kind").c_str());
            expect(c.viewsUnbound == 1 && c.viewSplitTargets == 0, (mode + "a view with no tagged draw is unbound").c_str());
        }
    }

    // 2c. The binding rule (CONTRACTS.md a.5.7), decided at ViewEnd: a view's first draw goes to a small target without a
    // depth test (the sky's pass), then a depth-tested draw into the frame samples it. The principal moves to the frame,
    // the small target is auxiliary and read, and the view is not split. A second view samples its own principal: split
    // (rule 4).
    {
        gt4::gsn::GsFrontend fe;
        fe.setSceneSplit(true);
        Gif g = setup();
        fe.gif(3, static_cast<const uint8_t*>(g.data()), g.bytes());
        std::vector<uint32_t> words(gt4::gfx::kModelWords, 0);
        words[64 * 4 + 3] = 1;
        words[65 * 4 + 3] = 3 * 3;
        gt4::gfx::ModelJob job{};
        job.count = 5;
        gt4::gfx::scene::FrameBegin begin{};
        begin.prefix = {gt4::gfx::scene::kFrameBegin, gt4::gfx::scene::kVersion};
        fe.sceneRecord(reinterpret_cast<const uint8_t*>(&begin), sizeof begin);
        auto state = [&](uint32_t fbp, uint32_t fbw, bool depthTest, uint64_t tex) {
            Gif s2;
            s2.ad({{FRAME_1, frame(fbp, fbw, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, !depthTest)}, {TEST_1, test(true, depthTest ? 2 : 1)},
                   {TEX0_1, tex}});
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
        };
        auto viewEnd = [&](uint16_t id) {
            gt4::gfx::scene::ViewEnd e{};
            e.prefix = {gt4::gfx::scene::kViewEnd, gt4::gfx::scene::kVersion};
            e.view = id;
            fe.sceneRecord(reinterpret_cast<const uint8_t*>(&e), sizeof e);
            auto package = std::make_shared<gt4::scene::SceneView>();
            package->view.id = id;
            fe.sceneView(std::move(package));
        };
        // view 1: the sky's pass into a 64 x 64 target at page 0x100, no depth test
        state(0x100, 1, false, 0);
        job.prim = 4;
        job.pad[0] = 1;
        job.pad[1] = 1;
        fe.model(job, words.data());
        // then the frame, depth-tested, sampling the sky's target
        state(0, 10, true, tex0(0x100 * 32, 1, PSMCT32, 6, 6, true, 0));
        job.prim = 4 | (1 << 4);
        job.pad[0] = 2;
        fe.model(job, words.data());
        viewEnd(1);
        // view 2: depth-tested into the frame, sampling the frame itself (its principal): rule 4
        state(0, 10, true, tex0(0, 10, PSMCT32, 10, 9, true, 0));
        job.pad[0] = 3;
        job.pad[1] = 2;
        fe.model(job, words.data());
        viewEnd(2);
        // view 3, as the race draws its main view: the frame depth-tested and written, then the sky's pass depth-tested
        // against a 64 x 64 Z of its own it never writes (rule 1: auxiliary, not a second scene): not split
        auto stateZ = [&](uint32_t fbp, uint32_t fbw, uint32_t zbp, bool zwrite) {
            Gif s2;
            s2.ad({{FRAME_1, frame(fbp, fbw, PSMCT32)}, {ZBUF_1, zbuf(zbp, PSMZ24, !zwrite)}, {TEST_1, test(true, 2)}, {TEX0_1, 0}});
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
        };
        job.prim = 4;
        job.pad[1] = 3;
        stateZ(0, 10, 0x70, true);
        job.pad[0] = 4;
        fe.model(job, words.data());
        stateZ(0x100, 1, 0x180, false);
        job.pad[0] = 5;
        fe.model(job, words.data());
        viewEnd(3);
        // one view per rule: each starts with the frame depth-tested and written (its principal), then
        auto principalDraw = [&](uint16_t id, uint32_t serial, uint64_t tex) {
            Gif s2;
            s2.ad({{FRAME_1, frame(0, 10, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, false)}, {TEST_1, test(true, 2)}, {TEX0_1, tex}});
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
            job.prim = tex ? (4 | (1 << 4)) : 4;
            job.pad[0] = serial;
            job.pad[1] = id;
            fe.model(job, words.data());
        };
        auto auxDraw = [&](uint16_t id, uint32_t serial, uint32_t fbp, uint32_t zbp, bool ztest, bool zwrite) {
            Gif s2;
            s2.ad({{FRAME_1, frame(fbp, 1, PSMCT32)}, {ZBUF_1, zbuf(zbp, PSMZ24, !zwrite)}, {TEST_1, test(true, ztest ? 2 : 1)}, {TEX0_1, 0}});
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
            job.prim = 4;
            job.pad[0] = serial;
            job.pad[1] = id;
            fe.model(job, words.data());
        };
        uint32_t serial = 10;
        principalDraw(4, ++serial, 0);   // rule 1: a second scene (another target's own depth, tested and written)
        auxDraw(4, ++serial, 0x100, 0x180, true, true);
        viewEnd(4);
        principalDraw(5, ++serial, 0);   // rule 2: another colour target testing the principal's depth, as PSMZ16 (another id)
        {
            Gif s2;
            s2.ad({{FRAME_1, frame(0x100, 1, PSMCT32)}, {ZBUF_1, zbuf(0x70, 0x32, true)}, {TEST_1, test(true, 2)}, {TEX0_1, 0}});
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
            job.prim = 4;
            job.pad[0] = ++serial;
            job.pad[1] = 5;
            fe.model(job, words.data());
        }
        viewEnd(5);
        principalDraw(6, ++serial, 0);   // rule 3: a target inside the principal's memory (page 0x40 of the frame)
        auxDraw(6, ++serial, 0x40, 0x70, false, false);
        viewEnd(6);
        principalDraw(7, ++serial, 0);   // rule 5: a fifth auxiliary target
        for (uint32_t k = 0; k < 5; ++k) auxDraw(7, ++serial, 0x100 + 0x10 * k, 0x70, false, false);
        viewEnd(7);
        principalDraw(8, ++serial, 0);   // a full summary: a ninth target
        for (uint32_t k = 0; k < 8; ++k) auxDraw(8, ++serial, 0x100 + 0x10 * k, 0x70, false, false);
        viewEnd(8);
        // an auxiliary read sampled before its target's first write, then after it (lane B's (1)): a read
        const uint64_t sky = tex0(0x1C0 * 32, 1, PSMCT32, 6, 6, true, 0);   // (page 0x1C0: GS memory has 512)
        auxDraw(0, 0, 0x1C0, 0x70, false, false);   // (untagged, before the view: the first sample resolves to that target)
        principalDraw(9, ++serial, sky);
        auxDraw(9, ++serial, 0x1C0, 0x70, false, false);
        principalDraw(9, ++serial, sky);
        viewEnd(9);
        auto principalZ = [&](uint16_t id, uint32_t zbp, bool ztest, bool zwrite) {
            Gif s2;
            s2.ad({{FRAME_1, frame(0, 10, PSMCT32)}, {ZBUF_1, zbuf(zbp, PSMZ24, !zwrite)}, {TEST_1, test(true, ztest ? 2 : 1)}, {TEX0_1, 0}});
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
            job.prim = 4;
            job.pad[0] = ++serial;
            job.pad[1] = id;
            fe.model(job, words.data());
        };
        principalZ(10, 0x150, false, true);   // D1 (page 0x150), written, untested
        principalZ(10, 0x1A0, true, false);   // D2 (page 0x1A0), tested
        principalZ(10, 0x150, true, true);    // D1 tested
        viewEnd(10);
        principalZ(11, 0x1A0, true, false);   // D2 alone: its id
        viewEnd(11);
        gt4::gsn::FrameList list;
        fe.cut(list);
        expect(list.views.size() == 11 && !list.views[2].splitTargets && list.views[2].auxCount == 1,
               "binding: a pass testing a depth of its own it never writes is auxiliary, not a second scene");
        if (list.views.size() == 11) {
            expect(list.views[9].depth != 0 && list.views[9].depth == list.views[10].depth,
                   "binding: the principal's depth is that of its first depth-tested draw, not its first depth");
            expect(list.views[3].splitTargets, "binding: rule 1, a second scene, splits");
            expect(list.views[4].splitTargets, "binding: rule 2, another colour target on the principal depth, splits");
            expect(list.views[5].splitTargets, "binding: rule 3, a target inside the principal's memory, splits");
            expect(list.views[6].splitTargets && list.views[6].auxCount == 4, "binding: rule 5, a fifth auxiliary target, splits");
            expect(list.views[7].splitTargets, "binding: a full summary splits");
            expect(!list.views[8].splitTargets && list.views[8].auxReads == 1,
                   "binding: a principal sample after the auxiliary target's write is a read, whatever came before");
        }
        expect(fe.stats.splitByReason[1] == 1 && fe.stats.splitByReason[2] == 1 && fe.stats.splitByReason[3] >= 1 &&
                   fe.stats.splitByReason[5] >= 1 && fe.stats.splitSummaryFull == 1,
               "binding: each rule counted, a full summary apart");
        expect(list.views.size() >= 2, "binding: two views' packages on the list");
        if (list.views.size() >= 2) {
            const auto& v1 = list.views[0];
            const auto& v2 = list.views[1];
            expect(v1.target != 0 && v1.depth != 0 && v1.auxCount == 1 && v1.auxTargets[0] != v1.target && v1.auxDraws == 1,
                   "binding: the principal moved to the depth-tested draw's target, the first one is auxiliary");
            expect(v1.auxReads == 1 && !v1.splitTargets, "binding: the principal's sample of the auxiliary target is a read, not a split");
            expect(v2.splitTargets && v2.target == v1.target, "binding: a view sampling its own principal is split");
        }
        expect(fe.stats.splitByReason[4] == 1, "binding: rule 4 counted once (the view sampling its own principal)");
    }

    // 2d. A5a's marker check (gs_frontend.cpp sceneFrame): a frame whose list has a Light command, its post and HUD markers'
    // command order. 'S' a 3D scene in the frame (68 triangles, depth-tested), 'L' a draw sampling it (the Light), '1'..'4'
    // PostBegin, PostEnd, HudBegin, HudEnd, 'C' a cut.
    {
        auto run = [&](const char* steps) {
            gt4::gsn::GsFrontend fe;
            Gif g = setup();
            fe.gif(3, static_cast<const uint8_t*>(g.data()), g.bytes());
            std::vector<uint32_t> words(gt4::gfx::kModelWords, 0);
            words[64 * 4 + 3] = 1;
            words[65 * 4 + 3] = 68 * 3;   // one strip of 70 vertices: 68 triangles (a scene needs 64)
            gt4::gfx::ModelJob job{};
            job.count = 70;
            gt4::gfx::scene::FrameBegin begin{};
            begin.prefix = {gt4::gfx::scene::kFrameBegin, gt4::gfx::scene::kVersion};
            fe.sceneRecord(reinterpret_cast<const uint8_t*>(&begin), sizeof begin);
            auto sf = std::make_unique<gt4::scene::SceneFrame>();
            gt4::gsn::FrameList cutList;
            for (const char* c = steps; *c; ++c) {
                if (*c == 'S' || *c == 'L') {
                    Gif s2;
                    if (*c == 'S')
                        s2.ad({{FRAME_1, frame(0, 10, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, false)}, {TEST_1, test(true, 2)}, {TEX0_1, 0}});
                    else
                        s2.ad({{FRAME_1, frame(0x100, 10, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, true)}, {TEST_1, test(true, 1)},
                               {TEX0_1, tex0(0, 10, PSMCT32, 10, 9, true, 0)}});
                    fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
                    job.prim = *c == 'S' ? 4 : (4 | (1 << 4));
                    fe.model(job, words.data());
                } else if (*c == 'C') {
                    fe.cut(cutList);
                } else {
                    gt4::gfx::scene::Marker m{};
                    m.prefix = {gt4::gfx::scene::kMarker, gt4::gfx::scene::kVersion};
                    m.marker = static_cast<uint16_t>(*c - '0');
                    fe.sceneRecord(reinterpret_cast<const uint8_t*>(&m), sizeof m);
                    sf->markers.push_back(m);   // (the decoder's list, in arrival order)
                }
            }
            fe.sceneFrame(std::move(sf));
            return fe.stats;
        };
        auto a = run("S1L234");
        expect(a.markersInOrder == 1 && a.lightInsidePost == 1, "markers: the Light inside the post, in order");
        auto b = run("SL1234");
        expect(b.markersInOrder == 1 && b.lightBeforePost == 1, "markers: the Light before the post, in order");
        auto c = run("S1L324");
        expect(c.markersOutOfOrder == 1 && c.markersInOrder == 0, "markers: HudBegin before PostEnd is out of order");
        auto d = run("SL");
        expect(d.markersAbsent == 1 && d.markersOutOfOrder == 0, "markers: a frame with a Light but no markers is counted absent");
        auto e = run("S1L2C34");
        expect(e.markersNotChecked == 1 && e.markersInOrder == 0 && e.markersOutOfOrder == 0, "markers: a frame cut by a read is not checked");
        auto f = run("S1L23434");
        expect(f.markersInOrder == 1, "markers: split screen's HUD pair per player, in sequence, in order");
        auto h = run("S1L2343");
        expect(h.markersOutOfOrder == 1, "markers: a HUD pair left open is out of order");
        auto i = run("S1L23");
        expect(i.markersOutOfOrder == 1 && i.markersAbsent == 0, "markers: the only HUD pair left open is out of order, not absent");
        auto j = run("S1L3");
        expect(j.markersOutOfOrder == 1 && j.markersAbsent == 0, "markers: both pairs begun, neither ended, is out of order");
        auto k = run("S1L");
        expect(k.markersAbsent == 1, "markers: a PostBegin and no HudBegin is a screen without the HUD (absent)");
    }

    // 2e. N4's marking (PLAN.md N4; gs_frontend.cpp bracketFor): draws inside the flare's and glare's markers are marked when
    // they go onto the picture (the main view's principal target, or a screen-sized target), never into the small sample
    // target the game reads back. Marked as the markers arrive, so a list cut inside the bracket is marked too; a marked
    // draw does not merge with an unmarked one. All three draw paths: per vertex (sprites), runs (vertices) and models.
    {
        gt4::gsn::GsFrontend fe;
        Gif g = setup();
        fe.gif(3, static_cast<const uint8_t*>(g.data()), g.bytes());
        std::vector<uint32_t> words(gt4::gfx::kModelWords, 0);
        words[64 * 4 + 3] = 1;
        words[65 * 4 + 3] = 3 * 3;
        gt4::gfx::ModelJob job{};
        job.count = 5;
        job.prim = 4;
        auto record = [&](const auto& r) { fe.sceneRecord(reinterpret_cast<const uint8_t*>(&r), sizeof r); };
        auto frameBegin = [&] {
            gt4::gfx::scene::FrameBegin b{};
            b.prefix = {gt4::gfx::scene::kFrameBegin, gt4::gfx::scene::kVersion};
            record(b);
        };
        auto marker = [&](uint16_t kind) {
            gt4::gfx::scene::Marker m{};
            m.prefix = {gt4::gfx::scene::kMarker, gt4::gfx::scene::kVersion};
            m.marker = kind;
            record(m);
        };
        // the 3D scene's state (depth-tested and written), 2D into a target (no depth), and the draws
        auto scene3d = [&] {
            Gif s2;
            s2.ad({{FRAME_1, frame(0, 10, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, false)}, {TEST_1, test(true, 2)}, {TEX0_1, 0},
                   {SCISSOR_1, scissor(0, 639, 0, 447)}});
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
        };
        auto target2d = [&](uint32_t fbp, uint32_t fbw, uint32_t height) {
            Gif s2;
            s2.ad({{FRAME_1, frame(fbp, fbw, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, true)}, {TEST_1, test(true, 1)}, {TEX0_1, 0},
                   {SCISSOR_1, scissor(0, fbw * 64 - 1, 0, height - 1)}});
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
        };
        auto drawSprite = [&](uint32_t w, uint32_t h) {
            Gif s2;
            sprite(s2, 0, 0, w, h, rgbaq(255, 200, 100, 0x80));
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
        };
        auto drawRun = [&] {   // a triangle strip as the 3D renderers send it (GsFrontend::vertices)
            gt4::gsn::GsFrontend::InputVertex v[3];
            const uint32_t xy[3][2] = {{10, 10}, {200, 10}, {10, 200}};
            for (int i = 0; i < 3; ++i) {
                v[i].x = (2048 + xy[i][0]) << 4;
                v[i].y = (2048 + xy[i][1]) << 4;
                v[i].rgba = 0x80FFFFFFu;
            }
            fe.setPrim(4);
            fe.vertices(v, 3, false);
        };
        auto drawModel = [&](uint16_t view, uint32_t serial) {
            job.pad[0] = serial;
            job.pad[1] = view;
            fe.model(job, words.data());
        };
        using gt4::gsn::kBracketFlare;
        using gt4::gsn::kBracketGlare;

        frameBegin();
        gt4::gfx::scene::ViewBegin vb{};
        vb.prefix = {gt4::gfx::scene::kViewBegin, gt4::gfx::scene::kVersion};
        vb.view = 1;
        vb.role = gt4::gfx::scene::kViewMain;
        record(vb);
        scene3d();
        drawModel(1, 1);                                   // the main view's scene: its principal is the frame at 0
        gt4::gfx::scene::ViewEnd ve{};
        ve.prefix = {gt4::gfx::scene::kViewEnd, gt4::gfx::scene::kVersion};
        ve.view = 1;
        record(ve);
        target2d(0, 10, 448);
        drawSprite(640, 448);                              // before the flare: not marked
        marker(gt4::gfx::scene::kMarkFlareBegin);
        drawSprite(640, 448);                              // the flare onto the principal: marked, not merged with the last
        target2d(0x1C0, 1, 8);
        drawSprite(64, 8);                                 // its sample into the 64 x 8 target: kept
        gt4::gsn::FrameList cutList;
        fe.cut(cutList);                                   // a read inside the bracket
        target2d(0, 10, 448);
        drawRun();                                         // a run onto the principal: marked
        scene3d();
        drawModel(0, 0);                                   // a model (untagged) onto the principal: marked
        target2d(0x100, 10, 448);
        drawSprite(640, 448);                              // onto another screen-sized target (the display buffer): marked
        marker(gt4::gfx::scene::kMarkFlareEnd);
        drawSprite(640, 448);                              // after the flare: not marked, not merged
        marker(gt4::gfx::scene::kMarkGlareBegin);
        drawSprite(640, 448);                              // the glare onto it: marked as glare
        marker(gt4::gfx::scene::kMarkGlareEnd);
        auto sf = std::make_unique<gt4::scene::SceneFrame>();
        gt4::scene::ViewData v1;
        v1.id = 1;
        v1.role = gt4::gfx::scene::kViewMain;
        sf->views.push_back(v1);
        fe.sceneFrame(std::move(sf));
        gt4::gsn::FrameList last;
        fe.vsync(1, last);

        auto brackets = [](const gt4::gsn::FrameList& l) {
            std::string s;
            for (const auto& c : l.commands)
                if (c.kind == gt4::gsn::FrameList::Command::Kind::Draw) s += char('0' + l.draws[c.index].bracket);
            return s;
        };
        const std::string cutMarks = brackets(cutList), lastMarks = brackets(last);
        expect(cutMarks == "0010", ("N4: the cut list's draws marked in the bracket, the sample kept (got " + cutMarks + ")").c_str());
        expect(!cutList.scene, "N4: the cut list has no scene frame, and is marked all the same");
        expect(lastMarks == "11102", ("N4: the run, the model and the display buffer's draw marked; glare apart (got " + lastMarks + ")").c_str());
        expect(fe.stats.bracketPrincipal[kBracketFlare] == 3 && fe.stats.bracketScreen[kBracketFlare] == 1 &&
                   fe.stats.bracketKept[kBracketFlare] == 1,
               "N4: the flare's draws counted by the rule that marked them, the sample as kept");
        expect(fe.stats.bracketScreen[kBracketGlare] == 1 && fe.stats.bracketPrincipal[kBracketGlare] == 0,
               "N4: the glare's draw counted apart");

        // the next frame: no main view bound, so the size rule; a FrameBegin closes a bracket left open
        frameBegin();
        marker(gt4::gfx::scene::kMarkFlareBegin);
        target2d(0, 10, 448);
        drawSprite(640, 448);
        expect(fe.stats.bracketsOpenAtEnd == 0, "N4: the first frame closed its brackets");
        frameBegin();
        drawSprite(640, 448);
        // a frame that ends with the flare's bracket open (its End lost): counted, and nothing after it is marked
        marker(gt4::gfx::scene::kMarkFlareBegin);
        fe.sceneFrame(std::make_unique<gt4::scene::SceneFrame>());
        drawSprite(640, 448);
        gt4::gsn::FrameList next;
        fe.cut(next);
        const std::string nextMarks = brackets(next);
        expect(nextMarks == "10", ("N4: without a main view the size rule marks; a new frame starts outside the brackets, and "
                                   "draws after a FrameEnd are outside them (got " + nextMarks + ")").c_str());
        expect(fe.stats.bracketScreen[kBracketFlare] == 2, "N4: that draw counted by the size rule");
        expect(fe.stats.bracketsOpenAtEnd == 1, "N4: a frame ending with a bracket open is counted");
    }
    // 2e'. N4 and the displayed buffer: the race shows a 640 x 224 field, under the size rule's 384 lines. A buffer the
    // display showed at the last VSync is the picture (rule 3); another 640 x 224 buffer, never shown, is not.
    {
        gt4::gsn::GsFrontend fe;
        Gif g = setup();
        fe.gif(3, static_cast<const uint8_t*>(g.data()), g.bytes());
        auto record = [&](const auto& r) { fe.sceneRecord(reinterpret_cast<const uint8_t*>(&r), sizeof r); };
        auto field = [&](uint32_t fbp) {   // a 640 x 224 sprite into the buffer at page fbp
            Gif s2;
            s2.ad({{FRAME_1, frame(fbp, 10, PSMCT32)}, {ZBUF_1, zbuf(0x70, PSMZ24, true)}, {TEST_1, test(true, 1)}, {TEX0_1, 0},
                   {SCISSOR_1, scissor(0, 639, 0, 223)}});
            sprite(s2, 0, 0, 640, 224, rgbaq(255, 200, 100, 0x80));
            fe.gif(3, static_cast<const uint8_t*>(s2.data()), s2.bytes());
        };
        field(0);                          // the field buffer at 0 (displayRecord's DISPFB), shown at a VSync
        field(0x100);                      // another 640 x 224 buffer, never shown
        fe.display(displayRecord());
        gt4::gsn::FrameList shownList;
        fe.vsync(1, shownList);
        gt4::gfx::scene::FrameBegin b{};
        b.prefix = {gt4::gfx::scene::kFrameBegin, gt4::gfx::scene::kVersion};
        record(b);
        gt4::gfx::scene::Marker m{};
        m.prefix = {gt4::gfx::scene::kMarker, gt4::gfx::scene::kVersion};
        m.marker = gt4::gfx::scene::kMarkFlareBegin;
        record(m);
        field(0);
        field(0x100);
        m.marker = gt4::gfx::scene::kMarkFlareEnd;
        record(m);
        fe.sceneFrame(std::make_unique<gt4::scene::SceneFrame>());
        gt4::gsn::FrameList list;
        fe.cut(list);
        std::string marks;
        for (const auto& c : list.commands)
            if (c.kind == gt4::gsn::FrameList::Command::Kind::Draw) marks += char('0' + list.draws[c.index].bracket);
        expect(shownList.display.target1 != 0, "N4: the display shows the field buffer at 0");
        expect(marks == "10", ("N4: the shown 224-line buffer is the picture, the other one is not (got " + marks + ")").c_str());
        expect(fe.stats.bracketDisplayed[gt4::gsn::kBracketFlare] == 1 && fe.stats.bracketScreen[gt4::gsn::kBracketFlare] == 0 &&
                   fe.stats.bracketKept[gt4::gsn::kBracketFlare] == 1,
               "N4: counted as a displayed buffer, the other as kept");
    }

    // 2f. gsn::carryOver's textures (a skipped list's, moved into the next): a reset in the next list drops them (they
    // came before it), a reset in the skipped list keeps them after it, and without a reset both lists' go in order.
    {
        using gt4::gsn::FrameList;
        auto texture = [](uint32_t id, uint32_t version) {
            gt4::gsn::TextureImage t;
            t.id = id;
            t.version = version;
            return t;
        };
        auto ids = [](const FrameList& l) {
            std::vector<uint32_t> out;
            for (const auto& t : l.textures) out.push_back(t.id * 10 + t.version);
            return out;
        };
        {   // the next list resets: the skipped list's uploads and retirements are gone with it
            FrameList dropped, into;
            dropped.textures.push_back(texture(1, 1));
            dropped.retiredTextures.push_back(2);
            into.resetTextures = true;
            into.textures.push_back(texture(3, 1));
            gt4::gsn::carryOver(dropped, into);
            expect(ids(into) == std::vector<uint32_t>{31}, "carryOver: a reset in the next list drops the skipped list's uploads");
            expect(into.retiredTextures.empty(), "carryOver: ... and its retirements");
            expect(into.resetTextures, "carryOver: the next list's reset stays");
        }
        {   // the skipped list resets: the reset, its uploads, then the next list's
            FrameList dropped, into;
            dropped.resetTextures = true;
            dropped.textures.push_back(texture(1, 1));
            dropped.retiredTextures.push_back(2);
            into.textures.push_back(texture(3, 1));
            gt4::gsn::carryOver(dropped, into);
            expect(ids(into) == (std::vector<uint32_t>{11, 31}), "carryOver: a reset in the skipped list keeps its uploads, before the next list's");
            expect(into.resetTextures, "carryOver: the skipped list's reset moves in");
            expect(into.retiredTextures == std::vector<uint32_t>{2}, "carryOver: the skipped list's retirements move in");
        }
        {   // no reset: both lists' uploads in order, the later version of a texture last
            FrameList dropped, into;
            dropped.textures.push_back(texture(5, 1));
            into.textures.push_back(texture(5, 2));
            gt4::gsn::carryOver(dropped, into);
            expect(ids(into) == (std::vector<uint32_t>{51, 52}), "carryOver: without a reset, the skipped list's uploads first");
            expect(!into.resetTextures, "carryOver: no reset made up");
        }
    }

    // 2g. B6 (design a92c4a2): each instance of a view gets its mesh and materials at ViewEnd, beside the package. Two
    // instances with the same batch share a mesh; a third's two jobs have a callback's untagged run between them (jobs by
    // tag, not a range); a skinned one gets mesh 0. The first frame finds every mesh new or pending; the second, after the
    // decodes, the same mesh and material ids, and the frame's instances get a copy.
    {
        using gt4::scene::InstanceData;
        gt4::gsn::GsFrontend fe;
        fe.setSceneSplit(true);
        Gif g = setup();
        fe.gif(3, static_cast<const uint8_t*>(g.data()), g.bytes());
        std::vector<uint32_t> a(gt4::gfx::kModelWords, 0), b(gt4::gfx::kModelWords, 0);
        for (auto* w : {&a, &b}) {
            (*w)[64 * 4 + 3] = 1;       // one strip
            (*w)[65 * 4 + 3] = 3 * 3;   // of 5 vertices: 3 triangles
        }
        b[0] = 0x3F800000;              // another position: another batch
        gt4::gfx::ModelJob job{};
        job.count = 5;
        job.prim = 4;
        auto frameOf = [&](uint32_t vsync, gt4::gsn::FrameList& out) {
            gt4::gfx::scene::FrameBegin begin{};
            begin.prefix = {gt4::gfx::scene::kFrameBegin, gt4::gfx::scene::kVersion};
            fe.sceneRecord(reinterpret_cast<const uint8_t*>(&begin), sizeof begin);
            auto tagged = [&](uint32_t instance, const std::vector<uint32_t>& words) {
                job.pad[0] = instance;
                job.pad[1] = instance ? 1 : 0;
                fe.model(job, words.data());
            };
            tagged(1, a);
            tagged(2, a);
            tagged(3, a);
            tagged(0, b);   // an item callback's untagged run, between instance 3's jobs
            tagged(3, b);
            tagged(4, a);
            gt4::gfx::scene::ViewEnd end{};
            end.prefix = {gt4::gfx::scene::kViewEnd, gt4::gfx::scene::kVersion};
            end.view = 1;
            fe.sceneRecord(reinterpret_cast<const uint8_t*>(&end), sizeof end);
            auto package = std::make_shared<gt4::scene::SceneView>();
            package->view.id = 1;
            package->view.valid = true;   // (its checks passed at ViewEnd: an invalid view gets no ids)
            auto frame = std::make_unique<gt4::scene::SceneFrame>();
            for (uint32_t serial = 1; serial <= 4; ++serial) {
                InstanceData in;
                in.serial = serial + 10;   // (a record's serial, not trusted: the ids follow the position)
                in.index = serial - 1;     // (its position: the jobs' tag - 1)
                in.view = 1;
                if (serial == 4) in.flags = gt4::gfx::scene::kInstSkinned;
                package->instances.push_back(in);
                frame->instances.push_back(in);
            }
            gt4::scene::ViewData v;
            v.id = 1;
            frame->views.push_back(v);
            fe.sceneView(std::move(package));
            fe.sceneFrame(std::move(frame));
            fe.vsync(vsync, out);
        };
        gt4::gsn::FrameList first, second;
        frameOf(1, first);
        expect(first.views.size() == 1 && first.views[0].ids.size() == 4, "B6: one InstanceIds per instance of the package");
        if (first.views.size() == 1 && first.views[0].ids.size() == 4 && first.scene) {
            const auto& ids = first.views[0].ids;
            const auto& m = first.views[0].materials;
            const auto& c = first.scene->counters;
            expect(ids[0].materialCount == 1 && ids[1].materialCount == 1 && ids[2].materialCount == 2 && ids[3].materialCount == 1,
                   "B6: a material per item, found by tag (instance 3's two jobs across the callback's)");
            expect(ids[0].mesh == 0 && ids[1].mesh == 0 && ids[2].mesh == 0 && ids[3].mesh == 0, "B6: no mesh before its decode");
            expect(c.meshesNew == 2 && c.meshesPending == 1, "B6: new meshes asked for (1 and 3), one asked again while decoding (2)");
            expect(c.meshesSkinned == 1, "B6: a skinned instance is counted, mesh 0");
            expect(m[ids[0].firstMaterial] == m[ids[1].firstMaterial] && c.materialsNew == 1,
                   "B6: the same state and texture, the same material (made once)");
        }
        fe.sceneCache()->meshes().wait();
        frameOf(2, second);
        if (second.views.size() == 1 && second.views[0].ids.size() == 4 && second.scene && first.views.size() == 1) {
            const auto& ids = second.views[0].ids;
            const auto& m = second.views[0].materials;
            expect(ids[0].mesh != 0 && ids[0].mesh == ids[1].mesh && ids[2].mesh != 0 && ids[2].mesh != ids[0].mesh && ids[3].mesh == 0,
                   "B6: after the decodes, two instances share a mesh, the third has its own, the skinned none");
            expect(m == first.views[0].materials, "B6: the same material ids in the next frame");
            expect(second.scene->counters.meshesNew == 0 && second.scene->counters.materialsNew == 0, "B6: nothing new the second time");
            const auto& in = second.scene->instances;
            expect(in.size() == 4 && in[2].mesh == ids[2].mesh && in[2].materialCount == 2 &&
                       second.scene->materials.size() == 5 && second.scene->materials[in[2].firstMaterial] == m[ids[2].firstMaterial],
                   "B6: the frame's instances get a copy of the ids");
        } else {
            expect(false, "B6: the second frame's view and scene");
        }
        expect(fe.stats.sceneInstances == 8 && fe.stats.sceneItems == 10, "B6: instances and items counted");
        // A VSync inside the view (lane A's review): its jobs before it went with that list, so the view is cut, no ids.
        gt4::gfx::scene::FrameBegin begin{};
        begin.prefix = {gt4::gfx::scene::kFrameBegin, gt4::gfx::scene::kVersion};
        fe.sceneRecord(reinterpret_cast<const uint8_t*>(&begin), sizeof begin);
        job.pad[0] = 1;
        job.pad[1] = 1;
        fe.model(job, a.data());
        gt4::gsn::FrameList before, after;
        fe.vsync(3, before);
        fe.model(job, a.data());
        gt4::gfx::scene::ViewEnd end{};
        end.prefix = {gt4::gfx::scene::kViewEnd, gt4::gfx::scene::kVersion};
        end.view = 1;
        fe.sceneRecord(reinterpret_cast<const uint8_t*>(&end), sizeof end);
        auto package = std::make_shared<gt4::scene::SceneView>();
        package->view.id = 1;
        package->view.valid = true;
        InstanceData in;
        in.serial = 1;
        in.view = 1;
        package->instances.push_back(in);
        fe.sceneView(std::move(package));
        fe.vsync(4, after);
        expect(after.views.size() == 1 && after.views[0].ids.empty(), "B6: a view a VSync cut gets no ids");
    }

    // 3. A capture replayed through GsHost (no game: reads stay in memory): one displayed list per VBlank.
    {
        const std::filesystem::path dir = outputDir.empty() ? gt4::test::uniqueTempPath("gt4_gs_host_test") : std::filesystem::path(outputDir);
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
        if (outputDir.empty()) std::filesystem::remove_all(dir);
    }

    if (failures) {
        std::fprintf(stderr, "gs_host: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("gs_host: all checks passed\n");
    return 0;
}
