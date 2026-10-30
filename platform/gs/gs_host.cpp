// PS2 graphics host: link / replay -> graphics stream -> the native renderer's front end, on a worker thread
// (see gs_host.h).
#include "gs_host.h"

#include "gs_frontend.h"

#include "link_host.h"
#include "stream.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
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
    }
    void setEventSink(gsn::GsFrontend::EventSink sink) { native->setEventSink(std::move(sink)); }
    void gif(uint32_t path, const uint8_t* data, size_t bytes) override { native->gif(path, data, bytes); }
    void primitives(const ps2::GsPrimitives& p) override {
        if (p.setPrim) native->setPrim(p.prim);
        for (size_t k = 0; k < p.count; ++k) {
            const ps2::GsVertex& v = p.vertices[k];
            native->vertex({v.s, v.t, v.q, v.rgba, v.x, v.y, v.z, v.fog, v.adc}, p.fog);
        }
    }
    void privileged(uint32_t offset, uint64_t value) override {
        // GT4_DISPFB=FBP[,FBW[,PSM]] (hex FBP): scan out that frame buffer instead (debugging: look at
        // an intermediate render target, e.g. the 3D before the post passes)
        static const char* over = std::getenv("GT4_DISPFB");
        if (over && (offset == 0x70 || offset == 0x90)) {
            unsigned fbp = 0, fbw = 10, psm = 0;
            std::sscanf(over, "%x,%u,%u", &fbp, &fbw, &psm);
            value = (value & ~0xFFFFFull) | fbp | (fbw << 9) | (static_cast<uint64_t>(psm) << 15);
        }
        native->privileged(offset, value);
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
            }
        }
        const bool statsNow = triggered || statsAt.second < 0 ||
                              (static_cast<long>(count) >= statsAt.first && static_cast<long>(count) <= statsAt.second);
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
                    for (uint32_t k = 0; k < d.count; ++k) {
                        const auto& v = list.vertices[d.first + k];
                        x0 = std::min(x0, v.x); x1 = std::max(x1, v.x); y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
                        u0 = std::min(u0, v.u); u1 = std::max(u1, v.u); v0 = std::min(v0, v.v); v1 = std::max(v1, v.v);
                    }
                    const uint64_t t0 = d.state.tex0;
                    std::printf("[gsn all] t%u bp %X frame %llX prim %03X | tbp %llX tbw %llu psm %llX %ux%u tfx %llu cbp %llX cpsm %llX csa %llu cld %llu | src %d:%u | alpha %llX test %llX sc %llX | n %u xy %.0f,%.0f-%.0f,%.0f uv %.0f,%.0f-%.0f,%.0f rgba %08X",
                                d.target, t ? t->bp : 0, (unsigned long long)d.state.frame, d.state.prim,
                                (unsigned long long)(t0 & 0x3FFF), (unsigned long long)((t0 >> 14) & 0x3F), (unsigned long long)((t0 >> 20) & 0x3F),
                                1u << ((t0 >> 26) & 0xF), 1u << ((t0 >> 30) & 0xF), (unsigned long long)((t0 >> 35) & 3),
                                (unsigned long long)((t0 >> 37) & 0x3FFF), (unsigned long long)((t0 >> 51) & 0xF), (unsigned long long)((t0 >> 56) & 0x1F),
                                (unsigned long long)(t0 >> 61), static_cast<int>(d.texture.kind), d.texture.id,
                                (unsigned long long)d.state.alpha, (unsigned long long)d.state.test, (unsigned long long)d.state.scissor, d.count, x0, y0, x1, y1, u0, v0, u1, v1,
                                d.count ? list.vertices[d.first].rgba : 0u);
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
                std::printf("[gsn draw] test %llX zbuf %llX frame %llX prim %03X tex0 %016llX src kind %d id %u off %d,%d clamp %llX tex1 %llX alpha %llX sc %llX, %u vertices\n", (unsigned long long)d.state.test, (unsigned long long)d.state.zbuf, (unsigned long long)d.state.frame, d.state.prim,
                            static_cast<unsigned long long>(d.state.tex0), static_cast<int>(d.texture.kind), d.texture.id, d.texture.offsetX,
                            d.texture.offsetY, static_cast<unsigned long long>(d.state.clamp), static_cast<unsigned long long>(d.state.tex1),
                            static_cast<unsigned long long>(d.state.alpha), static_cast<unsigned long long>(d.state.scissor), d.count);
                // GT4_GSN_VERTS=N: the first N vertices of each draw (default 6)
                static const uint32_t verts = std::getenv("GT4_GSN_VERTS") ? std::strtoul(std::getenv("GT4_GSN_VERTS"), nullptr, 10) : 6;
                for (uint32_t k = 0; k < std::min<uint32_t>(d.count, verts); ++k) {
                    const auto& v = list.vertices[d.first + k];
                    std::printf("    z %08X xy %.2f %.2f uv %.2f %.2f stq %.4f %.4f %.4f rgba %08X\n", v.z, v.x, v.y, v.u, v.v, v.s, v.t, v.q, v.rgba);
                }
            }
            uint32_t reinterpreted = 0;
            for (const auto& d : list.draws) reinterpreted += d.texture.reinterpreted ? 1 : 0;
            std::printf("[gsn] vblank %u: %u draws read a target in another format\n", count, reinterpreted);
            std::printf("[gsn] vblank %u: %zu draws (%u untextured, %u from memory, %u from targets), %zu vertices, %zu textures decoded, "
                        "%zu targets, display %u/%u (PMODE %llX SMODE2 %llX DISPFB1 %llX DISPLAY1 %llX DISPFB2 %llX DISPLAY2 %llX)\n",
                        count, list.draws.size(), untextured, memory, targets, list.vertices.size(),
                        list.textures.size(), list.targets.size(), list.display.target1, list.display.target2,
                        (unsigned long long)list.display.pmode, (unsigned long long)list.display.smode2,
                        (unsigned long long)list.display.dispfb1, (unsigned long long)list.display.display1,
                        (unsigned long long)list.display.dispfb2, (unsigned long long)list.display.display2);
            if (triggered) std::fflush(stdout);
        }
        // Both read circuits off (PMODE EN1 = EN2 = 0): nothing is displayed this VBlank.
        if (!(list.display.pmode & 3)) ++blankFrames;
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
                if ((served = serveRead(partial, request, pixels))) native->setReadData(std::move(pixels));
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
    void readVram(void* out) override { std::memcpy(out, native->memory().data(), gsn::GsMemory::kBytes); }
    void writeVram(const void* data) override { native->restoreMemory(data); }
    uint64_t blankFrames = 0, gpuReads = 0, staleReads = 0;
private:
    std::function<void(gsn::FrameList&)> publishNative;
    ReadService serveRead;
    std::unique_ptr<gsn::GsFrontend> native;
    gsn::FrameList list;
    bool stats = false;
};

}  // namespace

struct GsHost::Impl {
    Options options;
    std::unique_ptr<NativeTarget> target;
    std::unique_ptr<ps2::GraphicsStream> stream;
    std::unique_ptr<ps2::GameLink> link;
    std::unique_ptr<ps2::ReplaySource> replay;
    std::thread worker;
    std::atomic<bool> stop{false}, done{false};
    bool paused = false;   // the game was asked to pause (setPaused)
    int64_t gameOptions[8] = {-1, -1, -1, -1, -1, -1, -1, -1};   // gameplay option values sent (setGameOption)

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
        list = gsn::FrameList{};
        while (queued.size() > kMaxQueued) {
            gsn::carryOver(queued[0], queued[1]);
            queued.erase(queued.begin());
        }
    }
    void publishNative(gsn::FrameList& list) {
        const bool displayed = list.displayed;
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            queue(list);
        }
        if (displayed) ++vsyncs;
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
    void dispatch(const Gt4LinkRecord& header, const uint8_t* payload) {
        if (header.type == GT4_REC_VSYNC) ++vblankRecords;
        if (recorder) {
            if (recorder->vsyncs >= recorder->from && recorder->vsyncs < recorder->from + recorder->count) {
                if (!recorder->started && recorder->from) {   // mid-run: the state the replay starts from
                    std::vector<uint8_t> vram(gsn::GsMemory::kBytes);
                    target->readVram(vram.data());
                    const std::vector<uint8_t> state = stream->saveState(vram.data());
                    recorder->out->write(GT4_REC_STATE, state.data(), static_cast<uint32_t>(state.size()));
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

    void runLive() {
        auto onRecord = [this](const Gt4LinkRecord& h, const uint8_t* p) { dispatch(h, p); };
        int idle = 0;
        while (!stop) {
            if (link->poll(onRecord)) { idle = 0; continue; }
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
        auto onRecord = [this](const Gt4LinkRecord& h, const uint8_t* p) { dispatch(h, p); };
        if (replay->nextFrame(onRecord)) { ++replayFrame; return true; }
        if (options.loop && replay->frames()) {
            replay->rewind();
            replayFrame = 0;
            return replay->nextFrame(onRecord) ? (++replayFrame, true) : false;
        }
        return false;
    }

    void runReplayRealTime() {
        using clock = std::chrono::steady_clock;
        const auto period = std::chrono::nanoseconds(16683350);   // 1 / 59.94 Hz
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
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(stepMutex);
                stepCondition.wait(lock, [this] { return stop || stepsRequested > stepsDone; });
                if (stop) return;
            }
            const bool more = replayStep();
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
    impl->link.reset();   // asks the game to quit
    std::printf("GS: %s\n", summary().c_str());
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
    ReadService reads;
    if (!options.gamePath.empty())
        reads = [raw](gsn::FrameList& partial, const gsn::ReadRequest& r, std::vector<uint8_t>& out) { return raw->serveRead(partial, r, out); };
    h.target = std::make_unique<NativeTarget>([raw](gsn::FrameList& list) { raw->publishNative(list); }, reads);
    h.stream = std::make_unique<ps2::GraphicsStream>(*h.target);
    if (options.audio)
        h.stream->setAudioWriter([sink = options.audio](uint32_t stream, uint32_t rate, const int16_t* frames, size_t count) {
            if (stream == 0) sink(rate, frames, count);  // stream 0: the game's final mix
        });
    if (!options.gamePath.empty()) {
        ps2::GameLink::Options linkOptions;
        linkOptions.gamePath = options.gamePath;
        linkOptions.gameArgs = options.gameArgs;
        h.link = ps2::GameLink::start(linkOptions, error);
        if (!h.link) return nullptr;
        ps2::GameLink* link = h.link.get();
        h.stream->setHostWriter([link](uint32_t type, const void* payload, uint32_t size) { link->write(type, payload, size); });
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

bool GsHost::takeReadRequest(gsn::ReadRequest& request) {
    std::lock_guard<std::mutex> lock(impl->readMutex);
    if (impl->readTaken == impl->readAsked) return false;
    impl->readTaken = impl->readAsked;
    request = impl->readRequest;
    return true;
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

void GsHost::setGameOption(uint32_t id, uint32_t value) {
    if (!impl->link || !impl->link->running() || id >= 8 || impl->gameOptions[id] == value) return;
    impl->gameOptions[id] = value;
    const uint32_t payload[3] = {4u, id, value};
    impl->link->write(GT4_REC_HOST_CMD, payload, sizeof payload);
}

bool GsHost::live() const { return impl->link && impl->link->running(); }
bool GsHost::finished() const { return impl->done; }
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

std::string GsHost::summary() const {
    const Impl& h = *impl;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - h.started).count();
    std::ostringstream out;
    out << h.vsyncs.load() << " frames displayed in " << seconds << " s, " << h.events.load() << " GS events; "
        << h.stream->describe();
    if (h.target && h.target->blankFrames) out << "; " << h.target->blankFrames << " VBlanks with no display";
    if (h.target && (h.target->gpuReads || h.target->staleReads))
        out << "; " << h.target->gpuReads << " reads of drawn pixels from the GPU, " << h.target->staleReads << " from GS memory";
    return out.str();
}

}  // namespace gt4::gs
