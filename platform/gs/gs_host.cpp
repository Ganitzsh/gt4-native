// PS2 graphics host: link / replay -> graphics stream -> the native renderer's front end, on a worker thread
// (see gs_host.h).
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // GetThreadTimes (the summary's CPU times)
#endif
#include "gs_host.h"
#include "cpu_profile.h"

#include "glow_classifier.h"   // (F2b: the scene cache's material classifier)
#include "gs_frontend.h"

#include "host_mixer.h"
#include "link_host.h"
#include "stream.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdio>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace gt4::gs {

namespace {

// GT4_READ_STATS=1 (testing): per 60 reads of 32-bit pixels of each kind (waited for, asynchronous), the mean count of
// pixels not zero and their mean channel value: the flare pass's visibility both ways.
void readStats(bool async, const uint8_t* pixels, size_t bytes) {
    static const bool on = [] { const char* e = std::getenv("GT4_READ_STATS"); return e && *e && *e != '0'; }();
    if (!on || bytes != 64 * 8 * 4) return;   // (the flare target's 64 x 8)
    static uint64_t reads[2], lit[2], sum[2];
    const int k = async ? 1 : 0;
    for (size_t i = 0; i + 4 <= bytes; i += 4) {
        const uint32_t v = pixels[i] | (pixels[i + 1] << 8) | (pixels[i + 2] << 16) | (uint32_t(pixels[i + 3]) << 24);
        if (!v) continue;
        ++lit[k];
        sum[k] += pixels[i] + pixels[i + 1] + pixels[i + 2] + pixels[i + 3];
    }
    if (++reads[k] % 60) return;
    std::printf("read stats (%s): %.1f lit pixels a read, mean channel %.1f\n", async ? "asynchronous" : "waited for",
                double(lit[k]) / 60.0, lit[k] ? double(sum[k]) / double(lit[k] * 4) : 0.0);
    lit[k] = sum[k] = 0;
}

// A GS -> host read of drawn pixels, served by the render thread: the frame list cut at the read (drawn
// first) and the rectangle; false when it could not be served in time.
using ReadService = std::function<bool(gsn::FrameList& partial, const gsn::ReadRequest& request, std::vector<uint8_t>& out)>;

// The stream's GS: the native renderer's front end, whose frame lists the render thread draws.
class NativeTarget final : public ps2::GsTarget {
public:
    NativeTarget(std::function<void(gsn::FrameList&)> presentNative, ReadService readService)
        : publishNative(std::move(presentNative)), serveRead(std::move(readService)), native(std::make_unique<gsn::GsFrontend>()) {
        // GT4_GSN=stats prints a line per frame.
        const char* mode = std::getenv("GT4_GSN");
        stats = mode && !std::strcmp(mode, "stats");
        // GT4_GSN_SCENE_SPLIT=1 (A10, testing): Draw::view set, merging stopped at a view change (the pixels are the same
        // either way). Also on when the settings want it (GsHost::setSceneSplit: sceneSplitWanted), from the next VSync.
        if (const char* split = std::getenv("GT4_GSN_SCENE_SPLIT"); split && split[0] == '1') splitFromEnv = true;
        native->setSceneSplit(splitFromEnv);
        // F2b: the glow class of B6's materials (the night relight's course lamps), judged once per texture from its
        // summary at decode; each verdict on a texture with the street-lamp draw's state is logged once.
        glow->setLog(stdout);
        native->setSceneClassifier([fe = native.get(), g = glow](const scene::ItemSource& item) {
            scene::TextureSummary s;
            return g->classify(item, fe->textureSummary(item.texture, s) ? &s : nullptr);
        });
    }
    const render::GlowClassifier& glowClassifier() const { return *glow; }
    std::atomic<bool> splitWanted{false};   // GsHost::setSceneSplit (any thread); applied at the next vsync
    void setEventSink(gsn::GsFrontend::EventSink sink) { native->setEventSink(std::move(sink)); }
    // A live game's asynchronous reads (gfx::kReadAsync): the list cut at the read goes to the render thread with the
    // rectangle to copy when it draws it (`later`, not waited for), and GsHost::answerReads sends the pixels; pixels no
    // draw made are answered at once (`answer`).
    using Answer = std::function<void(uint32_t id, uint32_t qwc, const uint8_t* pixels)>;
    void setReadLater(std::function<void(gsn::FrameList&)> later, Answer answerNow) {
        queueLater = std::move(later);
        answer = std::move(answerNow);
    }
    bool readsLater() const override { return bool(queueLater); }
    bool readLater(uint32_t id, uint32_t qwc) override {
        if (!queueLater) return false;
        std::vector<uint8_t> pixels(size_t(qwc) * 16, 0);
        gsn::ReadRequest request;
        if (native->takeGpuRead(request) && request.async) {
            gsn::FrameList partial;
            native->cut(partial, true);   // (an asynchronous read cuts the list too: CONTRACTS.md a.8)
            partial.readbacks.push_back({request, id, qwc});
            native->readTransfer(pixels.data(), qwc);   // (the front end's read consumed; its memory model's pixels unused)
            queueLater(partial);
            ++laterReads;
        } else {
            native->readTransfer(pixels.data(), qwc);
            answer(id, qwc, pixels.data());
        }
        return true;
    }
    gsn::GsFrontend& frontend() { return *native; }
    void gif(uint32_t path, const uint8_t* data, size_t bytes) override { native->gif(path, data, bytes); }
    void state(const gfx::StateRecord& r) override { native->applyState(r); }
    void upload(const gfx::UploadRecord& u, const uint8_t* data, size_t bytes) override { native->upload(u, data, bytes); }
    void draw(const gfx::DrawRecord& d, const gfx::Vertex* vertices) override { native->draw(d, vertices); }
    void read(const gfx::ReadRecord& r) override { native->read(r); }
    void texSet(const uint8_t* payload, size_t bytes) override { native->texSet(payload, bytes); }
    // GT4_GPU_MODELS=0: the 3D renderers on the CPU, as before
    bool models() const override {
        static const bool on = [] { const char* e = std::getenv("GT4_GPU_MODELS"); return !(e && *e == '0'); }();
        return on;
    }
    void model(const gfx::ModelJob& job, const uint32_t* words) override { native->model(job, words); }
    void effect(const gfx::EffectRecord& r) override { native->effect(r); }
    // Scene records (CONTRACTS.md a.8): the front end follows the frames (cuts inside one), binds views to targets and
    // puts each closed frame on the FrameList that receives its FrameEnd.
    void scene(const uint8_t* payload, size_t bytes) override { native->sceneRecord(payload, bytes); }
    void sceneFrame(std::unique_ptr<scene::SceneFrame>& frame) override { native->sceneFrame(std::move(frame)); }
    // (packages only for the split's consumer). Asked on the stream thread (QueuedTarget forwards it), so from the
    // wanted state's atomic, not the front end's flag that vsync() writes on the GS thread: a package that arrives just
    // before the switch reaches sceneView with the split still off and gets no ids, which is harmless.
    bool wantsSceneViews() const override { return splitFromEnv || splitWanted.load(std::memory_order_relaxed); }
    void sceneView(std::shared_ptr<const scene::SceneView> view) override { native->sceneView(std::move(view)); }
    void primitives(const ps2::GsPrimitives& p) override {
        GT4_CPU_SCOPE("front end: the renderers' primitives");
        if (p.setPrim) native->setPrim(p.prim);
        static thread_local std::vector<gsn::GsFrontend::InputVertex> run;
        run.resize(p.count);
        for (size_t k = 0; k < p.count; ++k) {
            const ps2::GsVertex& v = p.vertices[k];
            run[k] = {v.s, v.t, v.q, v.rgba, v.x, v.y, v.z, v.fog, v.adc, {v.motion[0], v.motion[1], v.motion[2]}, {v.h[0], v.h[1], v.h[2], v.h[3]}};
        }
        native->vertices(run.data(), run.size(), p.fog);
    }
    void display(const gfx::DisplayRecord& d) override {
        // GT4_DISPFB=FBP[,FBW[,PSM]] (hex FBP, in 2048-word pages): show that frame buffer instead (debugging: look
        // at an intermediate render target, e.g. the 3D before the post passes)
        static const char* over = std::getenv("GT4_DISPFB");
        gfx::DisplayRecord shown = d;
        if (over) {
            unsigned fbp = 0, fbw = 10, psm = 0;
            std::sscanf(over, "%x,%u,%u", &fbp, &fbw, &psm);
            for (gfx::DisplayCircuit& c : shown.circuit) {
                c.base = static_cast<uint16_t>(fbp * 32);
                c.width = static_cast<uint8_t>(fbw);
                c.format = static_cast<uint8_t>(psm);
            }
        }
        native->display(shown);
    }
    void vsync(uint32_t field, uint32_t count) override {
        (void)field;
        {
            // GT4_GSN_VRAM=N: the front end's GS memory at the end of the frame before VBlank N (gsn_vram_N.bin).
            static const long dumpAt = std::getenv("GT4_GSN_VRAM") ? std::strtol(std::getenv("GT4_GSN_VRAM"), nullptr, 10) : -1;
            if (static_cast<long>(count) == dumpAt)
                if (FILE* f = std::fopen(("gsn_vram_" + std::to_string(count) + ".bin").c_str(), "wb")) {
                    std::fwrite(native->memory().data(), 1, gsn::GsMemory::kBytes, f);
                    std::fclose(f);
                }
            native->vsync(count, list);
            native->setSceneSplit(splitFromEnv || splitWanted.load(std::memory_order_relaxed));   // (between frames)
        }
        // GT4_GSN_AT=FROM[,TO] (debugging): the per-frame lines below only for those VBlanks.
        static const std::pair<long, long> statsAt = [] {
            long from = 0, to = -1;
            if (const char* e = std::getenv("GT4_GSN_AT")) {
                if (std::sscanf(e, "%ld,%ld", &from, &to) < 2) to = from;
            }
            return std::make_pair(from, to);
        }();
        // GT4_GSN_TRIGGER=PATH (debugging, live sessions): when that file appears it is deleted, this frame's lines
        // below are printed whatever GT4_GSN_AT says, and the native renderer dumps the frame's targets.
        bool triggered = false;
        if (static const char* trigger = std::getenv("GT4_GSN_TRIGGER"); trigger && native) {
            if (std::FILE* f = std::fopen(trigger, "rb")) {
                std::fclose(f);
                std::remove(trigger);
                triggered = true;
                list.dump = true;
                std::printf("[gsn] vblank %u: triggered\n", count);
                // and the front end's GS memory at the end of the frame (gsn_vram_N.bin)
                if (FILE* v = std::fopen(("gsn_vram_" + std::to_string(count) + ".bin").c_str(), "wb")) {
                    std::fwrite(native->memory().data(), 1, gsn::GsMemory::kBytes, v);
                    std::fclose(v);
                }
            }
        }
        const bool statsNow = triggered || statsAt.second < 0 ||
                              (static_cast<long>(count) >= statsAt.first && static_cast<long>(count) <= statsAt.second);
        // P2's metric (docs/modern/PLAN.md P2): what went into the GS memory model since the last VBlank: set pages by
        // cause (GsFrontend::MaterializeCause), bytes sent, bytes copied, CLUT loads.
        uint64_t memoryFrame[8] = {};
        if (native && stats) {
            const auto& s = native->stats;
            const uint64_t now[8] = {s.setPagesByCause[0], s.setPagesByCause[1], s.setPagesByCause[2], s.setPagesByCause[3],
                                     s.setPagesByCause[4], s.uploadBytes, s.copyBytes, s.clutLoads};
            for (int k = 0; k < 8; ++k) {
                memoryFrame[k] = now[k] - memoryBefore[k];
                memoryBefore[k] = now[k];
            }
        }
        if (native && stats && statsNow) {
            uint32_t memory = 0, targets = 0, untextured = 0;
            for (const auto& d : list.draws) {
                if (d.texture.kind == gsn::TextureSource::Kind::Memory) ++memory;
                else if (d.texture.kind == gsn::TextureSource::Kind::Target) ++targets;
                else ++untextured;
            }
            // GT4_GSN_DRAWS=BP: the draws into the target at that block address (hex), with their vertices.
            // GT4_GSN_DRAWS=all: one line per draw batch of every target (texture fields decoded, extents).
            static const char* drawsEnv = std::getenv("GT4_GSN_DRAWS");
            static const long drawsAt = drawsEnv ? std::strtol(drawsEnv, nullptr, 16) : -1;
            if (drawsEnv && !std::strcmp(drawsEnv, "all")) {
                for (const auto& d : list.draws) {
                    const gsn::Target* t = nullptr;
                    for (const auto& x : list.targets) if (x.id == d.target) t = &x;
                    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
                    for (uint32_t k = 0; k < (d.modeled ? 0u : d.count); ++k) {   // (3D on the GPU: not here)
                        const auto& v = list.vertices[d.first + k];
                        x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
                        u0 = std::min(u0, v.u); u1 = std::max(u1, v.u); v0 = std::min(v0, v.v); v1 = std::max(v1, v.v);
                    }
                    const gfx::Texture& t0 = d.state.c.texture;
                    const gfx::Context& c = d.state.c;
                    std::printf("[gsn all] t%u bp %X fb %X/%u psm %X keep %X prim %03X | tbp %X tbw %u psm %X %ux%u tfx %u cbp %X cpsm %X csa %u cld %u | src %d:%u | blend %u%u%u%u fix %X test a%u/%u/%X/%u d%u/%u z%u/%u sc %u-%u,%u-%u | n %u xy %.0f,%.0f-%.0f,%.0f uv %.0f,%.0f-%.0f,%.0f rgba %08X",
                                d.target, t ? t->bp : 0, c.colorBase, c.colorWidth, c.colorFormat, c.colorKeep, d.state.prim,
                                t0.base, t0.width, t0.format, 1u << t0.log2Width, 1u << t0.log2Height, t0.function, t0.clutBase, t0.clutFormat,
                                t0.clutOffset, t0.clutLoad, static_cast<int>(d.texture.kind), d.texture.id, c.blendA, c.blendB, c.blendC, c.blendD,
                                c.blendFixed, c.alphaTest, c.alphaFunc, c.alphaRef, c.alphaFail, c.destTest, c.destPass, c.depthTest, c.depthFunc,
                                c.scissorX0, c.scissorX1, c.scissorY0, c.scissorY1, d.count, x0, y0, x1, y1, u0, v0, u1, v1,
                                d.count && !d.modeled ? list.vertices[d.first].rgba : 0u);
                    if (d.texture.kind == gsn::TextureSource::Kind::Reinterpreted && d.texture.id < list.reinterpretations.size()) {
                        const auto& r = list.reinterpretations[d.texture.id];
                        std::printf(" | from t%u clut t%u", r.target, r.clutTarget);
                        if (!r.indices.empty()) {
                            std::printf(" indices");
                            for (size_t k = 0; k < std::min<size_t>(r.indices.size(), 16); ++k) std::printf(" %u", r.indices[k]);
                        }
                    }
                    std::printf("\n");
                }
            }
            for (const auto& d : list.draws) {
                const gsn::Target* t = nullptr;
                for (const auto& x : list.targets) if (x.id == d.target) t = &x;
                if (!t || static_cast<long>(t->bp) != drawsAt) continue;
                const gfx::Context& c = d.state.c;
                std::printf("[gsn draw] a%u/%u/%X/%u d%u/%u z%u/%u zb %X/%X/%u fb %X/%u/%X keep %X prim %03X tex %X/%u/%X %ux%u cbp %X src kind %d id %u off %d,%d "
                            "wrap %u/%u filter %u/%u/%u blend %u%u%u%u/%X sc %u-%u,%u-%u, %u vertices\n",
                            c.alphaTest, c.alphaFunc, c.alphaRef, c.alphaFail, c.destTest, c.destPass, c.depthTest, c.depthFunc, c.depthBase,
                            c.depthFormat, c.depthWrite, c.colorBase, c.colorWidth, c.colorFormat, c.colorKeep, d.state.prim, c.texture.base,
                            c.texture.width, c.texture.format, 1u << c.texture.log2Width, 1u << c.texture.log2Height, c.texture.clutBase,
                            static_cast<int>(d.texture.kind), d.texture.id, d.texture.offsetX, d.texture.offsetY, c.sampler.wrapU, c.sampler.wrapV,
                            c.sampler.magLinear, c.sampler.minFilter, c.sampler.mipMax, c.blendA, c.blendB, c.blendC, c.blendD, c.blendFixed,
                            c.scissorX0, c.scissorX1, c.scissorY0, c.scissorY1, d.count);
                // GT4_GSN_VERTS=N: the first N vertices of each draw (default 6)
                static const uint32_t verts = std::getenv("GT4_GSN_VERTS") ? std::strtoul(std::getenv("GT4_GSN_VERTS"), nullptr, 10) : 6;
                for (uint32_t k = 0; k < (d.modeled ? 0u : std::min<uint32_t>(d.count, verts)); ++k) {   // (3D on the GPU: not here)
                    const auto& v = list.vertices[d.first + k];
                    std::printf("    z %08X xy %.2f %.2f uv %.2f %.2f stq %.4f %.4f %.4f rgba %08X\n", v.z, v.x, v.y, v.u, v.v, v.s, v.t, v.q, v.rgba);
                }
            }
            uint32_t reinterpreted = 0;
            for (const auto& d : list.draws) reinterpreted += d.texture.reinterpreted ? 1 : 0;
            std::printf("[gsn] vblank %u: %u draws read a target in another format\n", count, reinterpreted);
            std::printf("[gsn] vblank %u: %zu draws (%u untextured, %u from memory, %u from targets), %zu vertices, %zu textures decoded, "
                        "%zu targets, display %u/%u (circuits %u%u, buffer %u width %u at %u,%u, %ux%u)\n",
                        count, list.draws.size(), untextured, memory, targets, list.vertices.size(),
                        list.textures.size(), list.targets.size(), list.display.target1, list.display.target2,
                        list.display.record.circuit[0].enabled, list.display.record.circuit[1].enabled,
                        list.display.record.shown().base, list.display.record.shown().width, list.display.record.shown().x,
                        list.display.record.shown().y, list.display.record.shownWidth(), list.display.record.shownHeight());
            const auto n = [&](int k) { return static_cast<unsigned long long>(memoryFrame[k]); };
            std::printf("[gsn] vblank %u: GS memory: %llu set pages (texture %llu, draw %llu, write %llu, read %llu, other %llu), "
                        "%llu bytes sent, %llu copied, %llu CLUT loads\n", count, n(0) + n(1) + n(2) + n(3) + n(4), n(0), n(1), n(2),
                        n(3), n(4), n(5), n(6), n(7));
            if (triggered) std::fflush(stdout);
        }
        // Both read circuits off: nothing is displayed this VBlank.
        if (!list.display.record.circuit[0].enabled && !list.display.record.circuit[1].enabled) ++blankFrames;
        publishNative(list);
    }
    void readTransfer(void* out, uint32_t qwc) override {
        // Pixels a draw made come from the GPU: the frame list so far is drawn, then the rectangle read back
        // (once per transfer; the reads that follow continue in it).
        gsn::ReadRequest request;
        if (native->takeGpuRead(request)) {
            bool served = false;
            if (serveRead) {
                gsn::FrameList partial;
                native->cut(partial);
                std::vector<uint8_t> pixels;
                if ((served = serveRead(partial, request, pixels))) {
                    readStats(false, pixels.data(), pixels.size());
                    native->setReadData(std::move(pixels));
                }
            }
            ++(served ? gpuReads : staleReads);
            if (!served && staleReads == 1)
                std::fprintf(stderr, "GS: a read of drawn pixels (target %u, %ux%u) was served from GS memory, without them\n",
                             request.target, request.width, request.height);
        }
        native->readTransfer(out, qwc);
        // GT4_GS_READ_DUMP=DIR (debugging): what each read returned, as DIR/gsread_N.bin.
        if (static const char* dir = std::getenv("GT4_GS_READ_DUMP"); dir) {
            static int n = 0;
            if (FILE* f = std::fopen((std::string(dir) + "/gsread_" + std::to_string(n++) + ".bin").c_str(), "wb")) {
                std::fwrite(out, 1, size_t(qwc) * 16, f);
                std::fclose(f);
            }
        }
    }
    void readVram(void* out) override {
        native->materializeAll();   // (the sets' pages too)
        std::memcpy(out, native->memory().data(), gsn::GsMemory::kBytes);
    }
    // readVram, and the pixels of every colour target still holding GS memory read back from the GPU into it: the
    // state a recording that starts mid-run begins with. A target drawn before the recording and only sampled after
    // it (the night sky of a race, drawn at its load) has its pixels on the GPU only; without them its replay samples
    // stale bytes until the game draws it again (lane F's r5_ss_ab2.cap, frames 130-190). Returns the targets read.
    size_t readVramWithTargets(void* out) {
        readVram(out);
        if (!serveRead) return 0;
        gsn::GsMemory copy, scratch;
        std::memcpy(copy.data(), out, gsn::GsMemory::kBytes);
        // (what is not drawn yet: drawn with the first read, once. This cuts the live run's list once at the recording's
        // start: with scene records on, that frame is a cut frame, its open views not substituted, markersNotChecked + 1;
        // one in a recorded session's summary is this, not a fault. Each read waits up to 1.5 s: a one-time stall)
        gsn::FrameList partial;
        native->cut(partial);
        bool partialSent = false;
        size_t read = 0;
        for (const gsn::GsFrontend::DrawnTarget& d : native->drawnTargets()) {
            std::vector<uint8_t> pixels;
            gsn::FrameList none;
            const bool ok = serveRead(partialSent ? none : partial, d.read, pixels);
            partialSent = true;
            if (!ok || pixels.empty()) continue;
            // written whole into a scratch copy of the state so far (a page it covers only in part keeps the rest), then
            // only the pages the target still owns copied into the state: a page another target or an upload holds
            // keeps that owner's bytes, whatever the order (lanes B and D)
            std::memcpy(scratch.data(), copy.data(), gsn::GsMemory::kBytes);
            gsn::GsMemory::Transfer tr;
            tr.dbp = d.bp;
            tr.dbw = d.bw;
            tr.dpsm = d.read.psm;
            tr.rrw = d.read.width;
            tr.rrh = d.read.height;
            scratch.beginWrite(tr);
            scratch.write(pixels.data(), pixels.size());
            for (const uint16_t page : d.pages)
                std::memcpy(copy.data() + size_t(page) * 8192, scratch.data() + size_t(page) * 8192, 8192);
            ++read;
        }
        std::memcpy(out, copy.data(), gsn::GsMemory::kBytes);
        return read;
    }
    void writeVram(const void* data) override { native->restoreMemory(data); }
    uint64_t blankFrames = 0, gpuReads = 0, staleReads = 0, laterReads = 0;
    uint64_t memoryBefore[8] = {};   // P2's metric at the last VBlank (vsync)
private:
    std::function<void(gsn::FrameList&)> publishNative;
    std::function<void(gsn::FrameList&)> queueLater;
    Answer answer;
    ReadService serveRead;
    std::unique_ptr<gsn::GsFrontend> native;
    gsn::FrameList list;
    bool stats = false;
    bool splitFromEnv = false;   // GT4_GSN_SCENE_SPLIT=1
    std::shared_ptr<render::GlowClassifier> glow = std::make_shared<render::GlowClassifier>();   // (F2b; the front end's thread)
};

}  // namespace

// The native renderer's front end on a thread of its own (GT4_GS_THREADS=1: on the GS thread, as before): the GS
// thread decodes the stream and runs GT4's 3D renderers while this one assembles what came before into frame
// lists (in a race the two took 24 ms a frame on one thread, the game waiting on them). Each call is recorded with
// its data into a chunk and the chunks are replayed in order; the calls that return data wait until the queue is
// empty and then run on the caller's thread, the front end's idle.
class QueuedTarget final : public ps2::GsTarget {
public:
    explicit QueuedTarget(ps2::GsTarget& inner) : inner(inner) { worker = std::thread([this] { run(); }); }
    ~QueuedTarget() override {
        submit();
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        worker.join();
    }
    void gif(uint32_t path, const uint8_t* data, size_t bytes) override { put(Op::Gif, path, 0, data, bytes); }
    void primitives(const ps2::GsPrimitives& p) override {
        put(Op::Primitives, (p.setPrim ? 1u : 0u) | (p.fog ? 2u : 0u), p.prim, p.vertices, p.count * sizeof(ps2::GsVertex));
    }
    void display(const gfx::DisplayRecord& d) override { put(Op::Display, 0, 0, &d, sizeof d); }
    void state(const gfx::StateRecord& r) override { put(Op::State, 0, 0, &r, sizeof r); }
    void upload(const gfx::UploadRecord& u, const uint8_t* data, size_t bytes) override {
        put(Op::Upload, 0, 0, &u, sizeof u, data, bytes);
    }
    void read(const gfx::ReadRecord& r) override { put(Op::Read, 0, 0, &r, sizeof r); }
    void texSet(const uint8_t* payload, size_t bytes) override { put(Op::TexSet, 0, 0, payload, bytes); }
    bool models() const override { return inner.models(); }
    void effect(const gfx::EffectRecord& r) override { put(Op::Effect, 0, 0, &r, sizeof r); }
    void originParts(const gfx::OriginParts& r) override { put(Op::OriginParts, 0, 0, &r, sizeof r); }
    void scene(const uint8_t* payload, size_t bytes) override { put(Op::Scene, 0, 0, payload, bytes); }
    void sceneFrame(std::unique_ptr<scene::SceneFrame>& frame) override {   // (the pointer travels; the front thread owns it)
        if (!frame) return;
        scene::SceneFrame* raw = frame.release();
        put(Op::SceneFrame, 0, 0, &raw, sizeof raw);
    }
    bool wantsSceneViews() const override { return inner.wantsSceneViews(); }
    void sceneView(std::shared_ptr<const scene::SceneView> view) override {   // (in a FIFO beside the queue: no allocation)
        {
            std::lock_guard<std::mutex> lock(viewsMutex);
            views.push_back(std::move(view));
        }
        put(Op::SceneView, 0, 0, nullptr, 0);
    }
    void model(const gfx::ModelJob& job, const uint32_t* words) override {
        put(Op::Model, 0, 0, &job, sizeof job, words, gfx::kModelWords * 4);
    }
    void draw(const gfx::DrawRecord& d, const gfx::Vertex* vertices) override {
        put(Op::Draw, 0, 0, &d, sizeof d, vertices, size_t(d.count) * sizeof(gfx::Vertex));
    }
    void vsync(uint32_t field, uint32_t count) override {
        put(Op::Vsync, field, count, nullptr, 0);
        submit();
    }
    void readTransfer(void* out, uint32_t qwc) override {
        drain();
        inner.readTransfer(out, qwc);
    }
    bool readsLater() const override { return inner.readsLater(); }
    bool readLater(uint32_t id, uint32_t qwc) override {   // (in order with the rest, not waited for)
        if (!inner.readsLater()) return false;
        put(Op::ReadLater, id, qwc, nullptr, 0);
        return true;
    }
    void readVram(void* out) override {
        drain();
        inner.readVram(out);
    }
    void writeVram(const void* data) override {
        drain();
        inner.writeVram(data);
    }
    // Everything recorded so far done by the front end.
    void drain() {
        submit();
        std::unique_lock<std::mutex> lock(mutex);
        idle.wait(lock, [&] { return queue.empty() && !busy; });
    }

private:
    enum class Op : uint32_t { Gif, Primitives, Display, State, Upload, Read, Draw, Vsync, TexSet, Model, Effect, ReadLater, Scene,
                               SceneFrame, SceneView, OriginParts };
    std::mutex viewsMutex;   // the views' FIFO: Op::SceneView takes the oldest
    std::vector<std::shared_ptr<const scene::SceneView>> views;
    size_t viewsHead = 0;
    struct Entry {
        Op op;
        uint32_t a, b;
        uint32_t first, second;   // byte sizes of the two payloads that follow (each padded to 16)
        uint32_t pad[3];
    };
    static_assert(sizeof(Entry) == 32);
    static size_t padded(size_t n) { return (n + 15) & ~size_t(15); }
    void put(Op op, uint32_t a, uint32_t b, const void* first, size_t firstSize, const void* second = nullptr, size_t secondSize = 0) {
        const size_t at = filling.size();
        filling.resize(at + sizeof(Entry) + padded(firstSize) + padded(secondSize));
        Entry e{op, a, b, static_cast<uint32_t>(firstSize), static_cast<uint32_t>(secondSize), {}};
        std::memcpy(filling.data() + at, &e, sizeof e);
        if (firstSize) std::memcpy(filling.data() + at + sizeof e, first, firstSize);
        if (secondSize) std::memcpy(filling.data() + at + sizeof e + padded(firstSize), second, secondSize);
        if (filling.size() >= (4u << 20)) submit();
    }
    // The chunk being recorded to the front end; at most a few frames ahead of it (the game would run ahead of
    // what is shown otherwise).
    void submit() {
        if (filling.empty()) return;
        std::unique_lock<std::mutex> lock(mutex);
        {
            GT4_STAGE_SCOPE("GS thread: waiting for the front end");
            idle.wait(lock, [&] { return queue.size() < 3; });
        }
        queue.push_back(std::move(filling));
        if (!spare.empty()) {
            filling = std::move(spare.back());
            spare.pop_back();
        }
        filling.clear();
        wake.notify_one();
    }
    void run() {
        for (;;) {
            std::vector<uint8_t> chunk;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stopping || !queue.empty(); });
                if (queue.empty()) return;
                chunk = std::move(queue.front());
                queue.pop_front();
                busy = true;
            }
            replay(chunk);
            {
                std::lock_guard<std::mutex> lock(mutex);
                busy = false;
                chunk.clear();
                if (spare.size() < 4) spare.push_back(std::move(chunk));
            }
            idle.notify_all();
        }
    }
    void replay(const std::vector<uint8_t>& chunk) {
        GT4_STAGE_SCOPE("front end thread: busy");
        size_t at = 0;
        while (at + sizeof(Entry) <= chunk.size()) {
            Entry e;
            std::memcpy(&e, chunk.data() + at, sizeof e);
            const uint8_t* first = chunk.data() + at + sizeof e;
            const uint8_t* second = first + padded(e.first);
            at += sizeof e + padded(e.first) + padded(e.second);
            switch (e.op) {
            case Op::Gif: inner.gif(e.a, first, e.first); break;
            case Op::Primitives: {
                ps2::GsPrimitives p;
                p.setPrim = (e.a & 1) != 0;
                p.fog = (e.a & 2) != 0;
                p.prim = e.b;
                p.vertices = reinterpret_cast<const ps2::GsVertex*>(first);
                p.count = e.first / sizeof(ps2::GsVertex);
                inner.primitives(p);
                break;
            }
            case Op::Display: inner.display(*reinterpret_cast<const gfx::DisplayRecord*>(first)); break;
            case Op::State: inner.state(*reinterpret_cast<const gfx::StateRecord*>(first)); break;
            case Op::Upload: inner.upload(*reinterpret_cast<const gfx::UploadRecord*>(first), second, e.second); break;
            case Op::Read: inner.read(*reinterpret_cast<const gfx::ReadRecord*>(first)); break;
            case Op::ReadLater: inner.readLater(e.a, e.b); break;
            case Op::Draw: inner.draw(*reinterpret_cast<const gfx::DrawRecord*>(first), reinterpret_cast<const gfx::Vertex*>(second)); break;
            case Op::Vsync: inner.vsync(e.a, e.b); break;
            case Op::TexSet: inner.texSet(first, e.first); break;
            case Op::Scene: inner.scene(first, e.first); break;
            case Op::SceneView: {
                std::shared_ptr<const scene::SceneView> view;
                {
                    std::lock_guard<std::mutex> lock(viewsMutex);
                    if (viewsHead < views.size()) view = std::move(views[viewsHead++]);
                    if (viewsHead == views.size()) {
                        views.clear();
                        viewsHead = 0;
                    }
                }
                inner.sceneView(std::move(view));
                break;
            }
            case Op::SceneFrame: {
                scene::SceneFrame* raw = nullptr;
                std::memcpy(&raw, first, sizeof raw);
                std::unique_ptr<scene::SceneFrame> frame(raw);
                inner.sceneFrame(frame);
                break;
            }
            case Op::Effect: {
                gfx::EffectRecord r;
                std::memcpy(&r, first, sizeof r);
                inner.effect(r);
                break;
            }
            case Op::OriginParts: {
                gfx::OriginParts r;
                std::memcpy(&r, first, sizeof r);
                inner.originParts(r);
                break;
            }
            case Op::Model: {
                gfx::ModelJob job;
                std::memcpy(&job, first, sizeof job);
                inner.model(job, reinterpret_cast<const uint32_t*>(second));
                break;
            }
            }
        }
    }

    ps2::GsTarget& inner;
    std::vector<uint8_t> filling;
    std::deque<std::vector<uint8_t>> queue;
    std::vector<std::vector<uint8_t>> spare;
    std::mutex mutex;
    std::condition_variable wake, idle;
    bool busy = false, stopping = false;
    std::thread worker;
};

struct GsHost::Impl {
    Options options;
    std::unique_ptr<NativeTarget> target;
    std::unique_ptr<QueuedTarget> frontThread;   // the front end's own thread, in front of target (GT4_GS_THREADS)
    std::unique_ptr<audio::HostMixer> mixer;     // the game's sound (platform/audio, PLAN.md D5); outlives the stream
    std::unique_ptr<ps2::GraphicsStream> stream;
    std::unique_ptr<ps2::GameLink> link;
    std::unique_ptr<ps2::ReplaySource> replay;
    std::thread worker;
    std::atomic<bool> stop{false}, done{false};
    std::atomic<bool> refused{false};   // a replayed capture of the old format (GIF / VIF records, PLAN.md P1)
    bool paused = false;   // the game was asked to pause (setPaused)
    // gameplay option values sent (setGameOption); -1: none yet
    std::array<int64_t, GT4_GAME_OPTION_COUNT> gameOptions = [] {
        std::array<int64_t, GT4_GAME_OPTION_COUNT> a;
        a.fill(-1);
        return a;
    }();

    mutable std::mutex frameMutex;

    std::mutex stepMutex;
    std::condition_variable stepCondition;
    uint64_t stepsRequested = 0, stepsDone = 0;

    std::atomic<uint64_t> vsyncs{0}, events{0};
    uint64_t replayFrame = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    // Frame lists not taken by the render thread yet, in order. Every list is drawn (later frames and GS ->
    // host reads see what it draws); past kMaxQueued (a render thread that stopped taking them) the oldest
    // lists' draws are dropped, their texture uploads kept.
    std::vector<gsn::FrameList> queued;
    static constexpr size_t kMaxQueued = 64;
    void queue(gsn::FrameList& list) {
        queued.push_back(std::move(list));
        list = gsn::spareFrameList();
        while (queued.size() > kMaxQueued) {
            gsn::carryOver(queued[0], queued[1]);
            queued.erase(queued.begin());
        }
    }
    // Displayed lists and read requests so far, and as many as waitNativeFrames has seen (under frameMutex).
    std::condition_variable frameReady;
    uint64_t arrivals = 0, arrivalsSeen = 0;
    void publishNative(gsn::FrameList& list) {
        const bool displayed = list.displayed;
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            queue(list);
            if (displayed) ++arrivals;
        }
        if (displayed) {
            ++vsyncs;
            frameReady.notify_all();
        }
    }

    // An asynchronous read's list (NativeTarget::readLater): queued like any other, drawn with the next frame.
    void queueReadLater(gsn::FrameList& partial) {
        std::lock_guard<std::mutex> lock(frameMutex);
        queue(partial);
    }

    // GS -> host reads of drawn pixels, from the worker to the render thread (takeReadRequest/completeRead).
    std::mutex readMutex;
    std::condition_variable readDone;
    gsn::ReadRequest readRequest;
    uint64_t readAsked = 0, readTaken = 0, readAnswered = 0;
    bool readOk = false;
    std::vector<uint8_t> readResult;
    bool serveRead(gsn::FrameList& partial, const gsn::ReadRequest& request, std::vector<uint8_t>& out) {
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            queue(partial);
        }
        std::unique_lock<std::mutex> lock(readMutex);
        readRequest = request;
        const uint64_t id = ++readAsked;
        {
            std::lock_guard<std::mutex> frames(frameMutex);
            ++arrivals;
        }
        frameReady.notify_all();
        // The game waits up to 2 s for its pixels; the render thread takes the request within a frame.
        if (!readDone.wait_for(lock, std::chrono::milliseconds(1500), [&] { return readAnswered == id || stop; })) return false;
        if (readAnswered != id || !readOk) return false;
        out = std::move(readResult);
        return true;
    }

    // GT4_HOST_RECORD=PATH[,FROM[,COUNT]] (debugging): the records the host receives, VBlanks FROM..FROM+COUNT,
    // as a capture for --replay (what a live run saw, which a game-side --capture does not reproduce).
    // FROM = trigger:FILE: from the VBlank at which FILE appears (it is deleted), for screens reached by hand.
    struct Recorder {
        std::unique_ptr<ps2::CaptureWriter> out;
        uint64_t from = 0, count = ~0ull, vsyncs = 0;
        bool started = false;
        std::string trigger;
    };
    std::unique_ptr<Recorder> recorder = [] {
        const char* e = std::getenv("GT4_HOST_RECORD");
        if (!e) return std::unique_ptr<Recorder>();
        auto r = std::make_unique<Recorder>();
        std::string path = e;
        const size_t comma = path.find(',');
        if (comma != std::string::npos && path.compare(comma + 1, 8, "trigger:") == 0) {
            std::string rest = path.substr(comma + 9);
            const size_t c2 = rest.find(',');
            if (c2 != std::string::npos) {
                r->count = std::strtoull(rest.c_str() + c2 + 1, nullptr, 10);
                rest.resize(c2);
            }
            r->trigger = rest;
            r->from = ~0ull;
            path.resize(comma);
        } else if (comma != std::string::npos) {
            unsigned long long from = 0, count = ~0ull;
            std::sscanf(path.c_str() + comma + 1, "%llu,%llu", &from, &count);
            r->from = from;
            r->count = count;
            path.resize(comma);
        }
        r->out = std::make_unique<ps2::CaptureWriter>(path);
        return r;
    }();
    std::atomic<uint64_t> vblankRecords{0};
    std::atomic<bool> contentsWanted{false};   // the game to send every texture content again (GT4_REC_HOST_CMD 5)
    // GT4_CPU_FROM=VBLANK or trigger:FILE: the summary's CPU times from that VBlank on (a race, not the boot and the
    // menus before it). The GS thread takes the starting point.
    struct CpuStart {
        bool armed = false, taken = false;
        uint64_t from = 0;
        std::string trigger;
        double game = 0.0, gsThread = 0.0;
        uint64_t vsyncs = 0;
    } cpuStart = [] {
        CpuStart c;
        if (const char* e = std::getenv("GT4_CPU_FROM")) {
            c.armed = true;
            if (!std::strncmp(e, "trigger:", 8)) c.trigger = e + 8;
            else c.from = std::strtoull(e, nullptr, 10);
        }
        return c;
    }();
    double gsThreadAtEnd = -1.0;   // the GS thread's own CPU time when it stopped
    double gameCpuAtEnd = -1.0;    // the game process's CPU time (ms) when the link closed
    void dispatch(const Gt4LinkRecord& header, const uint8_t* payload) {
        GT4_CPU_SCOPE("GS: dispatch");
        if (header.type == GT4_REC_VSYNC) {
            ++vblankRecords;
            if (stream->takeContentsLost()) contentsWanted = true;   // (sent by the main thread: sendRequests)
            static const uint64_t forgetAt = std::getenv("GT4_TEST_FORGET_UPLOADS") ? std::strtoull(std::getenv("GT4_TEST_FORGET_UPLOADS"), nullptr, 10) : 0;
            if (forgetAt && vblankRecords == forgetAt) stream->forgetUploadsForTest();
            if (cpuStart.armed && !cpuStart.taken) {
                bool now = cpuStart.trigger.empty() && vblankRecords >= cpuStart.from;
                if (!cpuStart.trigger.empty())
                    if (std::FILE* f = std::fopen(cpuStart.trigger.c_str(), "rb")) {
                        std::fclose(f);
                        std::remove(cpuStart.trigger.c_str());
                        now = true;
                    }
                if (now) {
                    cpuStart.taken = true;
                    cpuStart.game = link ? link->processSeconds() * 1000.0 : 0.0;
                    cpuStart.gsThread = threadCpuMilliseconds();
                    cpuStart.vsyncs = vsyncs.load();
                    cpuprofile::reset();
                    std::printf("GS: CPU times from VBlank %llu\n", static_cast<unsigned long long>(vblankRecords.load()));
                }
            }
        }
        if (recorder) {
            if (recorder->vsyncs >= recorder->from && recorder->vsyncs < recorder->from + recorder->count) {
                if (!recorder->started && recorder->from) {   // mid-run: the state the replay starts from
                    std::vector<uint8_t> vram(gsn::GsMemory::kBytes);
                    if (frontThread) frontThread->drain();   // the front end's thread idle from here
                    // (with the drawn targets' pixels from the GPU: what is sampled later without being drawn again)
                    if (const size_t drawn = target->readVramWithTargets(vram.data()))
                        std::printf("GS: recording from here: %zu drawn targets read back from the GPU into the state\n", drawn);
                    const std::vector<uint8_t> state = stream->saveState(vram.data());
                    recorder->out->write(GT4_REC_STATE, state.data(), static_cast<uint32_t>(state.size()));
                    // the set upload contents the runtime counts on the host keeping (gfx::kDataKeep), oldest first
                    stream->exportUploads([&](const std::vector<uint8_t>& bytes) {
                        std::vector<uint8_t> data(8 + bytes.size());
                        const uint32_t head[2] = {0, static_cast<uint32_t>(bytes.size()) | gfx::kDataKeep};
                        std::memcpy(data.data(), head, 8);
                        std::memcpy(data.data() + 8, bytes.data(), bytes.size());
                        recorder->out->write(GT4_REC_GFX_DATA, data.data(), static_cast<uint32_t>(data.size()));
                    });
                    // the native texture sets in graphics memory (docs/NATIVE_V2.md), as set records with their bytes
                    const size_t setsMissing = target->frontend().exportSets([&](const gfx::TexSetRecord& r, const std::vector<gfx::TexSetUpload>& uploads,
                                                      const std::vector<uint8_t>& bytes) {
                        size_t at = 0;
                        for (const gfx::TexSetUpload& u : uploads) {
                            if (!r.sent) break;
                            const uint32_t n = u.qwords * 16;
                            std::vector<uint8_t> data(8 + n, 0);
                            const uint32_t head[2] = {u.source, n};
                            std::memcpy(data.data(), head, 8);
                            if (at < bytes.size()) std::memcpy(data.data() + 8, bytes.data() + at, std::min<size_t>(n, bytes.size() - at));
                            at += n;
                            recorder->out->write(GT4_REC_GFX_DATA, data.data(), static_cast<uint32_t>(data.size()));
                        }
                        const uint32_t payload = static_cast<uint32_t>(sizeof r + uploads.size() * sizeof(gfx::TexSetUpload));
                        std::vector<uint8_t> block(gfx::recordQwords(payload) * 16, 0);
                        const gfx::Header hd = gfx::header(gfx::kTexSet, payload);
                        std::memcpy(block.data(), &hd, sizeof hd);
                        std::memcpy(block.data() + sizeof hd, &r, sizeof r);
                        std::memcpy(block.data() + sizeof hd + sizeof r, uploads.data(), uploads.size() * sizeof(gfx::TexSetUpload));
                        recorder->out->write(GT4_REC_GFX, block.data(), static_cast<uint32_t>(block.size()));
                    });
                    // (a set in graphics memory whose content the front end no longer holds is not in the state: its
                    // textures replay from whatever the slot holds; lane F's r5_ss_ab2.cap, frames 154-161)
                    if (setsMissing)
                        std::printf("GS: recording from here: %zu texture sets in graphics memory without their content, "
                                    "left out of the state\n", setsMissing);
                    // the model packets' contents the runtime counts on the host having (content only, sent 2)
                    stream->exportModels([&](uint64_t hash, const std::vector<uint8_t>& bytes) {
                        std::vector<uint8_t> data(8 + bytes.size());
                        const uint32_t head[2] = {0, static_cast<uint32_t>(bytes.size())};
                        std::memcpy(data.data(), head, 8);
                        std::memcpy(data.data() + 8, bytes.data(), bytes.size());
                        recorder->out->write(GT4_REC_GFX_DATA, data.data(), static_cast<uint32_t>(data.size()));
                        const gfx::BatchModel m{0, static_cast<uint32_t>(bytes.size() / 16), 0, 2, static_cast<uint32_t>(hash), static_cast<uint32_t>(hash >> 32)};
                        std::vector<uint8_t> block(gfx::recordQwords(sizeof m) * 16, 0);
                        const gfx::Header hd = gfx::header(gfx::kBatchModel, sizeof m);
                        std::memcpy(block.data(), &hd, sizeof hd);
                        std::memcpy(block.data() + sizeof hd, &m, sizeof m);
                        recorder->out->write(GT4_REC_GFX, block.data(), static_cast<uint32_t>(block.size()));
                    });
                    // last, the record data still waiting for its record (a set's uploads sent before the start, its
                    // record after it): the records that take it after the start find it, as they did live
                    stream->exportPendingData([&](uint32_t source, uint32_t field, const std::vector<uint8_t>& payload) {
                        std::vector<uint8_t> data(8 + payload.size());
                        const uint32_t head[2] = {source, field};
                        std::memcpy(data.data(), head, 8);
                        if (!payload.empty()) std::memcpy(data.data() + 8, payload.data(), payload.size());
                        recorder->out->write(GT4_REC_GFX_DATA, data.data(), static_cast<uint32_t>(data.size()));
                    });
                }
                recorder->started = true;
                recorder->out->write(header.type, payload, header.size);
            }
            if (header.type == GT4_REC_VSYNC) {
                ++recorder->vsyncs;
                if (!recorder->trigger.empty() && recorder->from == ~0ull) {
                    if (std::FILE* f = std::fopen(recorder->trigger.c_str(), "rb")) {
                        std::fclose(f);
                        std::remove(recorder->trigger.c_str());
                        recorder->from = recorder->vsyncs;
                        std::printf("GS: recording from VBlank %llu\n", static_cast<unsigned long long>(recorder->from));
                    }
                }
                recorder->out->flush();   // a run that loses the device still leaves its frames
            }
        }
        stream->record(header, payload);
    }

    // The GS thread's CPU time when its loop ends (the summary's; a joined thread's handle no longer answers).
    struct NoteCpu {
        Impl* impl;
        ~NoteCpu() { impl->gsThreadAtEnd = threadCpuMilliseconds(); }
    };
    void runLive() {
        NoteCpu note{this};
        auto onRecord = [this](const Gt4LinkRecord& h, const uint8_t* p) { dispatch(h, p); };
        int idle = 0;
        while (!stop) {
            bool got;
            {
                GT4_STAGE_SCOPE("GS thread: the game's records");
                got = link->poll(onRecord);
            }
            if (got) { idle = 0; continue; }
            if (!link->running()) {
                // Drain what the game wrote before exiting.
                while (link->poll(onRecord)) {}
                done = true;
                break;
            }
            // Spin briefly, then sleep: the game sends a burst of records per VBlank.
            if (++idle < 64) std::this_thread::yield();
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool replayStep() {
        GT4_CPU_SCOPE("GS: replay frame (reading and dispatch)");
        auto onRecord = [this](const Gt4LinkRecord& h, const uint8_t* p) { dispatch(h, p); };
        if (replay->nextFrame(onRecord)) {
            ++replayFrame;
            if (stream->retired()) {   // an old capture: refused, not drawn wrong (PLAN.md P1)
                refused = true;
                return false;
            }
            return true;
        }
        if (options.loop && replay->frames()) {
            replay->rewind();
            replayFrame = 0;
            return replay->nextFrame(onRecord) ? (++replayFrame, true) : false;
        }
        return false;
    }

    void runReplayRealTime() {
        NoteCpu note{this};
        using clock = std::chrono::steady_clock;
        const auto period = std::chrono::nanoseconds(16666667);   // 1 / 60 Hz (the game's frame clock)
        auto next = clock::now();
        while (!stop) {
            if (!replayStep()) { done = true; break; }
            next += period;
            const auto now = clock::now();
            if (next > now) std::this_thread::sleep_for(next - now);
            else next = now;
        }
    }

    void runReplayLockstep() {
        NoteCpu note{this};
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(stepMutex);
                stepCondition.wait(lock, [this] { return stop || stepsRequested > stepsDone; });
                if (stop) return;
            }
            const bool more = replayStep();
            if (frontThread) frontThread->drain();   // the step's frames ready when advance() returns
            {
                std::lock_guard<std::mutex> lock(stepMutex);
                ++stepsDone;
                if (!more) done = true;
            }
            stepCondition.notify_all();
            if (!more) return;
        }
    }
};

GsHost::GsHost() : impl(std::make_unique<Impl>()) {}

GsHost::~GsHost() {
    if (!impl) return;
    impl->stop = true;
    impl->stepCondition.notify_all();
    impl->readDone.notify_all();
    if (impl->worker.joinable()) impl->worker.join();
    impl->frontThread.reset();   // the front end finishes what was queued
    if (impl->link) impl->gameCpuAtEnd = impl->link->processSeconds() * 1000.0;
    impl->link.reset();   // asks the game to quit
    if (impl->stream) impl->stream->finishScene();   // a scene frame still open: framesTruncated (CONTRACTS.md a.5.1)
    std::printf("GS: %s\n", summary().c_str());
    cpuprofile::print(impl->vsyncs.load() - (impl->cpuStart.taken ? impl->cpuStart.vsyncs : 0));   // GT4_CPU_PROFILE
    std::fflush(stdout);
    // Markers for the teardown hang seen after long live runs (main.cpp's watchdog ends the process).
    impl->stream.reset();
    std::fprintf(stderr, "GS: stream stopped\n");
    impl->target.reset();
    std::fprintf(stderr, "GS: front end stopped\n");
}

std::unique_ptr<GsHost> GsHost::create(const Options& options, std::string& error) {
    if (options.gamePath.empty() == options.replayPath.empty()) {
        error = "give either a game to start or a capture to replay";
        return nullptr;
    }
    std::unique_ptr<GsHost> host(new GsHost());
    Impl& h = *host->impl;
    h.options = options;
    if (!options.replayPath.empty()) {
        h.replay = ps2::ReplaySource::open(options.replayPath, error);
        if (!h.replay) return nullptr;
        std::printf("Replay: %s, %zu VBlanks\n", options.replayPath.c_str(), h.replay->frames());
    }
    Impl* raw = &h;
    // Reads of drawn pixels go to the render thread in a live game; a replay has no game to answer (its
    // reads come from GS memory), and its lockstep would wait on the render thread that waits on it.
    // GT4_REPLAY_READS=1 (testing, a replay in real time): from the render thread all the same, as a live game's
    // (GsnScene::read's frame skipping: GT4_READ_SKIP_TEST, GT4_READ_HASH).
    static const bool replayReads = [] { const char* e = std::getenv("GT4_REPLAY_READS"); return e && *e && *e != '0'; }();
    ReadService reads;
    if (!options.gamePath.empty() || (replayReads && !options.lockstep))
        reads = [raw](gsn::FrameList& partial, const gsn::ReadRequest& r, std::vector<uint8_t>& out) { return raw->serveRead(partial, r, out); };
    h.target = std::make_unique<NativeTarget>([raw](gsn::FrameList& list) { raw->publishNative(list); }, reads);
    if (!options.texturePack.empty()) {
        auto pack = std::make_shared<gsn::TexturePack>();
        if (pack->open(options.texturePack)) {
            pack->setSynchronous(options.texturePackSynchronous);
            std::printf("Texture pack: %zu replacements in %s (%s)\n", pack->count(), options.texturePack.c_str(),
                        options.texturePackSynchronous ? "decoded at first use: fixed step" : "loaded in the background");
            h.target->frontend().setTexturePack(std::move(pack));
        } else {
            std::fprintf(stderr, "Texture pack: no replacements in %s\n", options.texturePack.c_str());
        }
    }
    h.target->frontend().setReplacements(options.hdTextures);
    // GT4_GS_THREADS=1: the front end on the GS thread (QueuedTarget otherwise).
    const char* threads = std::getenv("GT4_GS_THREADS");
    if (!(threads && std::atoi(threads) == 1)) h.frontThread = std::make_unique<QueuedTarget>(*h.target);
    h.stream = std::make_unique<ps2::GraphicsStream>(h.frontThread ? static_cast<ps2::GsTarget&>(*h.frontThread) : *h.target);
    // The game's sound (platform/audio/host_mixer.h): its own mix (stream 0), or with audio.mixer = host the SPU2's
    // records mixed here and BGM and movies (stream 1) added. GT4_HOST_AUDIO_WAV records it without an output too.
    if (options.audio || std::getenv("GT4_HOST_AUDIO_WAV")) {
        h.mixer = std::make_unique<audio::HostMixer>(options.audio);
        h.stream->setAudioWriter(
            [m = h.mixer.get()](uint32_t stream, uint32_t rate, const int16_t* frames, size_t count) {
                m->audio(stream, rate, frames, count);
            });
        h.stream->setSpuWriter([m = h.mixer.get()](uint32_t type, const uint8_t* payload, uint32_t size) {
            m->record(type, payload, size);
        });
    }
    if (options.rumble) h.stream->setRumbleWriter(options.rumble);
    if (options.ui) h.stream->setUiWriter(options.ui);
    if (options.settingsSet) h.stream->setSettingsSetWriter(options.settingsSet);
    if (!options.gamePath.empty()) {
        ps2::GameLink::Options linkOptions;
        linkOptions.gamePath = options.gamePath;
        linkOptions.gameArgs = options.gameArgs;
        h.link = ps2::GameLink::start(linkOptions, error);
        if (!h.link) return nullptr;
        ps2::GameLink* link = h.link.get();
        h.stream->setHostWriter([link](uint32_t type, const void* payload, uint32_t size) { link->write(type, payload, size); });
        h.target->setReadLater([raw](gsn::FrameList& partial) { raw->queueReadLater(partial); },
                               [link](uint32_t id, uint32_t qwc, const uint8_t* pixels) {
                                   std::vector<uint8_t> record(8 + size_t(qwc) * 16);
                                   std::memcpy(record.data(), &id, 4);
                                   std::memcpy(record.data() + 4, &qwc, 4);
                                   std::memcpy(record.data() + 8, pixels, size_t(qwc) * 16);
                                   link->write(GT4_REC_GS_IMAGE, record.data(), static_cast<uint32_t>(record.size()));
                               });
        // SIGNAL / FINISH / LABEL as any path writes them: the game applies CSR, IMR and SIGLBLID.
        h.target->setEventSink([raw, link](uint32_t kind, uint64_t payload) {
            struct { uint32_t kind, id; uint64_t value; } event{kind, static_cast<uint32_t>(payload), payload};
            link->write(GT4_REC_GS_EVENT, &event, sizeof(event));
            ++raw->events;
        });
        h.worker = std::thread([raw] { raw->runLive(); });
    } else {
        h.target->setEventSink([raw](uint32_t, uint64_t) { ++raw->events; });
        h.worker = options.lockstep ? std::thread([raw] { raw->runReplayLockstep(); }) : std::thread([raw] { raw->runReplayRealTime(); });
    }
    return host;
}

uint64_t GsHost::vblanks() const { return impl->vblankRecords.load(); }
uint64_t GsHost::drawnFrames() const { return impl->vsyncs.load(); }
void GsHost::setFrameDrawn(uint64_t vblank) {
    if (impl->link) impl->link->setHostFrameDrawn(static_cast<uint32_t>(vblank));
}
void GsHost::hostWaits(uint32_t& count, uint32_t& ms) const {
    count = ms = 0;
    if (impl->link) impl->link->gameHostWaits(count, ms);
}

bool GsHost::advance() {
    Impl& h = *impl;
    if (!h.replay || !h.options.lockstep) return !h.done;
    std::unique_lock<std::mutex> lock(h.stepMutex);
    if (h.done) return false;
    const uint64_t target = ++h.stepsRequested;
    h.stepCondition.notify_all();
    h.stepCondition.wait(lock, [&] { return h.stepsDone >= target || h.stop; });
    return !h.done;
}

bool GsHost::takeNativeFrames(std::vector<gsn::FrameList>& lists) {
    std::lock_guard<std::mutex> lock(impl->frameMutex);
    if (impl->queued.empty()) return false;
    for (auto& list : impl->queued) lists.push_back(std::move(list));
    impl->queued.clear();
    return true;
}

bool GsHost::waitNativeFrames(int ms) {
    std::unique_lock<std::mutex> lock(impl->frameMutex);
    const bool came = impl->frameReady.wait_for(lock, std::chrono::milliseconds(ms),
                                                [&] { return impl->arrivals != impl->arrivalsSeen; });
    impl->arrivalsSeen = impl->arrivals;
    return came;
}

double GsHost::gameCpuSeconds() const { return impl->link ? impl->link->processSeconds() : 0.0; }

bool GsHost::takeReadRequest(gsn::ReadRequest& request) {
    std::lock_guard<std::mutex> lock(impl->readMutex);
    if (impl->readTaken == impl->readAsked) return false;
    impl->readTaken = impl->readAsked;
    request = impl->readRequest;
    return true;
}

void GsHost::answerRead(uint32_t id, uint32_t qwc, const std::vector<uint8_t>& pixels) {
    if (!impl->link || !impl->link->running()) return;
    readStats(true, pixels.data(), pixels.size());
    std::vector<uint8_t> record(8 + size_t(qwc) * 16, 0);
    std::memcpy(record.data(), &id, 4);
    std::memcpy(record.data() + 4, &qwc, 4);
    std::memcpy(record.data() + 8, pixels.data(), std::min(pixels.size(), size_t(qwc) * 16));
    impl->link->write(GT4_REC_GS_IMAGE, record.data(), static_cast<uint32_t>(record.size()));
}

bool GsHost::readRequested() const {
    std::lock_guard<std::mutex> lock(impl->readMutex);
    return impl->readTaken != impl->readAsked;
}

void GsHost::completeRead(bool ok, std::vector<uint8_t>&& pixels) {
    {
        std::lock_guard<std::mutex> lock(impl->readMutex);
        impl->readAnswered = impl->readTaken;
        impl->readOk = ok;
        impl->readResult = std::move(pixels);
    }
    impl->readDone.notify_all();
}

void GsHost::sendPad(uint32_t port, bool connected, const uint8_t data[18]) {
    if (!impl->link || !impl->link->running()) return;
    uint8_t payload[26];
    const uint32_t c = connected ? 1u : 0u;
    std::memcpy(payload, &port, 4);
    std::memcpy(payload + 4, &c, 4);
    std::memcpy(payload + 8, data, 18);
    impl->link->write(GT4_REC_PAD, payload, sizeof(payload));
}

void GsHost::setPaused(bool paused) {
    if (!impl->link || !impl->link->running() || impl->paused == paused) return;
    impl->paused = paused;
    const uint32_t command = paused ? 2u : 3u;
    impl->link->write(GT4_REC_HOST_CMD, &command, sizeof command);
}

void GsHost::sendSettings(const std::vector<uint8_t>& payload) {
    if (!impl->link || !impl->link->running() || payload.empty()) return;
    impl->link->write(GT4_REC_SETTINGS, payload.data(), static_cast<uint32_t>(payload.size()));
}

void GsHost::sendMenuCommand(uint32_t command) {
    if (!impl->link || !impl->link->running() || (command != 6 && command != 7)) return;
    impl->link->write(GT4_REC_HOST_CMD, &command, sizeof command);
}

void GsHost::setHdTextures(bool on) { impl->target->frontend().setReplacements(on); }

void GsHost::sendRequests() {
    if (!impl->link || !impl->link->running() || !impl->contentsWanted.exchange(false)) return;
    const uint32_t command = 5;   // send every texture content again (runtime/frame.cpp)
    impl->link->write(GT4_REC_HOST_CMD, &command, sizeof command);
}

void GsHost::setAudioInterpolation(uint32_t mode) {
    if (impl->mixer && mode <= 2) impl->mixer->setInterpolation(static_cast<spu::Interpolation>(mode));
}

void GsHost::setGameOption(uint32_t id, uint32_t value) {
    if (!impl->link || !impl->link->running() || id >= GT4_GAME_OPTION_COUNT || impl->gameOptions[id] == value)
        return;
    impl->gameOptions[id] = value;
    const uint32_t payload[3] = {4u, id, value};
    impl->link->write(GT4_REC_HOST_CMD, payload, sizeof payload);
}

void GsHost::setSceneSplit(bool on) {
    if (impl->target) impl->target->splitWanted.store(on, std::memory_order_relaxed);
}

bool GsHost::live() const { return impl->link && impl->link->running(); }
bool GsHost::finished() const { return impl->done; }
bool GsHost::refused() const { return impl->refused; }
int GsHost::gameExitCode() const { return impl->link && !impl->link->running() ? impl->link->exitCode() : 0; }

std::string GsHost::status() const {
    const Impl& h = *impl;
    std::ostringstream out;
    if (h.link) {
        out << "game " << (h.link->running() ? "running" : "exited (" + std::to_string(h.link->exitCode()) + ")")
            << "  vblanks " << h.vsyncs.load() << "  " << h.link->bytesReceived() / (1024 * 1024) << " MiB";
    } else if (h.replay) {
        out << "replay " << h.replayFrame << "/" << h.replay->frames() << (h.done ? " (end)" : "");
    }
    return out.str();
}

double threadCpuMilliseconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0.0;
    auto ms = [](const FILETIME& t) { return double((uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-4; };
    return ms(kernel) + ms(user);
#else
    return 0.0;
#endif
}

double processCpuMilliseconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
    auto ms = [](const FILETIME& t) { return double((uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-4; };
    return ms(kernel) + ms(user);
#else
    return 0.0;
#endif
}

std::string GsHost::summary() const {
    const Impl& h = *impl;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - h.started).count();
    std::ostringstream out;
    out << h.vsyncs.load() << " frames displayed in " << seconds << " s, " << h.events.load() << " GS events; "
        << h.stream->describe();
    // CPU time a frame: the game's process (every thread), the GS thread (the stream, the 3D renderers, the
    // native renderer's front end); from GT4_CPU_FROM's point when it was reached.
    const bool fromStart = h.cpuStart.taken;
    if (const uint64_t frames = h.vsyncs.load() - (fromStart ? h.cpuStart.vsyncs : 0)) {
        out << "; CPU a frame" << (fromStart ? " (from GT4_CPU_FROM)" : "") << ":";
        const double game = h.link ? h.link->processSeconds() * 1000.0 : h.gameCpuAtEnd;
        if (game >= 0.0) out << " game " << (game - (fromStart ? h.cpuStart.game : 0.0)) / double(frames) << " ms,";
        double gs = h.gsThreadAtEnd;
#ifdef _WIN32
        FILETIME created, exited, kernel, user;
        if (gs < 0.0 && h.worker.joinable() &&
            GetThreadTimes(static_cast<HANDLE>(const_cast<std::thread&>(h.worker).native_handle()), &created, &exited, &kernel, &user)) {
            auto ms = [](const FILETIME& t) { return double((uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-4; };
            gs = ms(kernel) + ms(user);
        }
#endif
        if (gs >= 0.0) out << " GS thread " << (gs - (fromStart ? h.cpuStart.gsThread : 0.0)) / double(frames) << " ms";
    }
    if (h.target && h.target->blankFrames) out << "; " << h.target->blankFrames << " VBlanks with no display";
    if (h.target && h.target->frontend().stats.named) {
        const auto& s = h.target->frontend().stats;
        out << "; texture pack: " << s.named << " textures looked up, " << s.found << " found, " << s.replaced << " drawn replaced";
    }
    if (h.target && (h.target->frontend().stats.setTextures || h.target->frontend().stats.setMisses)) {
        const auto& s = h.target->frontend().stats;
        out << "; native textures: " << s.setTextures << " lookups by set, " << s.setMisses << " not held, " << s.setDecodes << " decoded, " << s.streamed << " stream frames in place, " << s.setPagesWritten << " set pages into memory, "
            << s.byAddress << " lookups by address (" << s.targetReads << " of targets, " << s.reinterpreted << " reinterpreted)";
    }
    if (h.target && h.vsyncs.load()) {   // P2's metric (docs/modern/PLAN.md P2), a frame
        const auto& s = h.target->frontend().stats;
        const double f = static_cast<double>(h.vsyncs.load());
        out << "; GS memory a frame: " << s.setPagesWritten / f << " set pages (texture " << s.setPagesByCause[0] / f << ", draw "
            << s.setPagesByCause[1] / f << ", write " << s.setPagesByCause[2] / f << ", read " << s.setPagesByCause[3] / f << ", other "
            << s.setPagesByCause[4] / f << "), " << s.uploadBytes / 1024.0 / f << " KiB sent, " << s.copyBytes / 1024.0 / f << " KiB copied, "
            << s.clutLoads / f << " CLUT loads; " << s.depthTextures << " Z buffers read as Z textures (GPU), " << s.readFallbacks
            << " reads of a target from memory (fallbacks)";
    }
    if (h.target && (h.target->gpuReads || h.target->staleReads))
        out << "; " << h.target->gpuReads << " reads of drawn pixels from the GPU, " << h.target->staleReads << " from GS memory";
    if (h.target && h.target->frontend().stats.readMismatchKindsSeen) {   // reads of drawn pages the GPU can't answer
        const auto& s = h.target->frontend().stats;
        out << "; reads of drawn pixels left to the memory model (target psm > read psm):";
        for (uint32_t k = 0; k < s.readMismatchKindsSeen; ++k)
            out << (k ? ", " : " ") << std::hex << unsigned(s.readMismatches[k].targetPsm) << " > " << unsigned(s.readMismatches[k].readPsm)
                << std::dec << " " << s.readMismatches[k].count;
    }
    if (h.target && h.target->frontend().stats.sceneFrames) {   // the front end's scene linkage (CONTRACTS.md a.7, a.8; A10)
        const auto& s = h.target->frontend().stats;
        out << "; scene front end: " << s.sceneFrames << " frames on lists, split " << (h.target->frontend().sceneSplitOn() ? "on" : "off")
            << " (" << s.sceneSplitDraws << " draws it adds), " << s.viewSplitTargets << " views split across targets, "
            << s.viewsUnbound << " unbound, " << s.cuts << " cuts in a frame (" << s.cutsAsync << " asynchronous), " << s.viewsCut
            << " views cut, " << s.scenePackages << " view packages, " << gsn::listsSkippedWithScene() << " lists skipped with their scene";
        auto byRole = [&](const char* what, const uint64_t (&n)[16]) {   // (role: count, where any)
            std::string list;
            for (int r = 0; r < 16; ++r)
                if (n[r]) list += (list.empty() ? "" : " ") + std::to_string(r) + ":" + std::to_string(n[r]);
            if (!list.empty()) out << "; " << what << " by role " << list;
        };
        byRole("split", s.splitByRole);
        byRole("auxiliary draws", s.auxDrawsByRole);
        byRole("auxiliary reads", s.auxReadsByRole);
        if (s.viewLightMapDraws) out << "; " << s.viewLightMapDraws << " light-map draws (out of the binding) into "
                                     << s.lightMapTargets.size() << " targets";
        if (s.viewReflectionDraws)
            out << "; " << s.viewReflectionDraws << " reflection draws (out of the binding) into "
                << s.reflectionTargets.size() << " targets (at most " << s.reflectionTargetsInFrame
                << " in a frame), sampled in their frame by " << s.reflectionSamples[0] << " untagged and "
                << s.reflectionSamples[1] << " tagged draws";
        std::string reasons;
        for (int r = 1; r <= 5; ++r)
            if (s.splitByReason[r]) reasons += (reasons.empty() ? "" : " ") + std::to_string(r) + ":" + std::to_string(s.splitByReason[r]);
        if (!reasons.empty()) out << "; split by reason " << reasons;
        if (s.splitSummaryFull) out << "; split with a full binding summary " << s.splitSummaryFull;
        if (s.viewsPastLimit) out << "; " << s.viewsPastLimit << " tagged draws naming a view past the limit";
        if (s.markersInOrder || s.markersOutOfOrder || s.markersNotChecked || s.markersAbsent)
            out << "; frames with a Light: post and HUD markers after it in order " << s.markersInOrder << ", not " << s.markersOutOfOrder
                << ", without the post or the HUD " << s.markersAbsent << " (the Light before PostBegin " << s.lightBeforePost
                << ", inside the post " << s.lightInsidePost << "); scene frames not checked for markers (a cut or VSync inside) "
                << s.markersNotChecked;
        // N4: Draws made inside the flare's and glare's brackets, by the rule that marked them; frames ending with one open
        for (const auto& [name, k] : {std::pair<const char*, uint8_t>{"flare", gsn::kBracketFlare}, {"glare", gsn::kBracketGlare}})
            if (s.bracketPrincipal[k] || s.bracketScreen[k] || s.bracketDisplayed[k] || s.bracketKept[k])
                out << "; " << name << " bracket draws onto the principal " << s.bracketPrincipal[k] << ", screen-sized "
                    << s.bracketScreen[k] << ", a displayed buffer " << s.bracketDisplayed[k] << ", kept (another target) "
                    << s.bracketKept[k];
        if (s.bracketsOpenAtEnd) out << "; scene frames ending with a bracket open " << s.bracketsOpenAtEnd;
        if (s.glowsChecked)
            out << "; flare glows with a Light at their centre " << s.glowsWithLight << " of " << s.glowsChecked
                << " (largest distance " << s.glowMaxDistance << " NDC)";
        // B6: the scene cache's ids (instances and items given them, the front-end time, the caches' totals, and whether
        // the same object kept its ids)
        if (gt4::scene::SceneCache* cache = h.target->frontend().sceneCache()) {
            const auto m = cache->meshes().stats();
            const auto t = cache->materials().stats();
            const auto o = cache->stats();
            out << "; scene cache: " << s.sceneInstances << " instances (" << s.sceneItems << " items) given ids in "
                << static_cast<uint64_t>(s.sceneCacheMicroseconds) << " us on the front end (" << (s.sceneFrames ? s.sceneCacheMicroseconds / double(s.sceneFrames) : 0.0)
                << " us a scene frame; thread CPU " << static_cast<uint64_t>(s.sceneCacheCpuMicroseconds) << " us, "
                << (s.sceneFrames ? s.sceneCacheCpuMicroseconds / double(s.sceneFrames) : 0.0)
                << " us a scene frame, cycles calibrated once against the steady clock); instances' meshes new " << s.meshesNew << ", pending " << s.meshesPending << ", skinned "
                << s.meshesSkinned << ", per-frame " << s.meshesPerFrame << ", out of the binding " << s.meshesOutOfBinding
                << ", materials new " << s.materialsNew
                << "; meshes " << m.decoded << " decoded, " << m.hits << " hits, " << m.perFrame << " per-frame, "
                << m.evicted << " evicted, " << (m.bytes >> 20) << " MiB held; materials " << t.created << " made, " << t.held
                << " held; objects " << o.objects << ", sightings with the same ids " << o.objectsSame << ", another mesh "
                << o.objectsMeshChanged << ", other materials " << o.objectsMaterialsChanged;
            // F2b: the glow class (materials judged, of them with the street-lamp draw's state, glows, without a texture
            // summary; textures judged) and the summaries' time at decode
            const auto g = h.target->glowClassifier().stats();
            out << "; glow class: " << g.judged << " materials judged, " << g.signature << " with the lamp state, " << g.glows
                << " glows, " << g.noSummary << " without a summary, " << g.textures << " textures; texture summaries "
                << s.textureSummaries << " in " << static_cast<uint64_t>(s.textureSummaryMicroseconds) << " us";
        }
        byRole("unbound", s.unboundByRole);
        byRole("cut", s.cutByRole);
    }
    return out.str();
}

}  // namespace gt4::gs
