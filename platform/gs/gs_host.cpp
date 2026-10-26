// PS2 graphics host: link / replay -> graphics stream -> GS back end, on a worker thread (see gs_host.h).
#include "gs_host.h"

#include "link_host.h"
#include "stream.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

namespace gt4::gs {

namespace {

// The stream's GS, implemented by the back end.
class BackendTarget final : public ps2::GsTarget {
public:
    BackendTarget(Backend& gs, std::function<void(DisplayFrame&)> present) : backend(gs), publish(std::move(present)) {}
    void gif(uint32_t path, const uint8_t* data, size_t bytes) override { backend.gif(path, data, bytes); }
    void privileged(uint32_t offset, uint64_t value) override { backend.writePrivileged(offset, value); }
    void vsync(uint32_t field, uint32_t count) override {
        if (backend.vsync(field, count, frame)) publish(frame);
        else ++blankFrames;
    }
    void readTransfer(void* out, uint32_t qwc) override { backend.readTransfer(out, qwc); }
    uint64_t blankFrames = 0;
private:
    Backend& backend;
    std::function<void(DisplayFrame&)> publish;
    DisplayFrame frame;
};

}  // namespace

struct GsHost::Impl {
    Options options;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<BackendTarget> target;
    std::unique_ptr<ps2::GraphicsStream> stream;
    std::unique_ptr<ps2::GameLink> link;
    std::unique_ptr<ps2::ReplaySource> replay;
    std::thread worker;
    std::atomic<bool> stop{false}, done{false};

    mutable std::mutex frameMutex;
    std::condition_variable frameReady;
    DisplayFrame latest;
    uint64_t serial = 0;

    std::mutex stepMutex;
    std::condition_variable stepCondition;
    uint64_t stepsRequested = 0, stepsDone = 0;

    std::atomic<uint64_t> vsyncs{0}, events{0};
    uint64_t replayFrame = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    void publish(DisplayFrame& frame) {
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            std::swap(latest, frame);
            ++serial;
        }
        ++vsyncs;
        frameReady.notify_all();
    }

    void dispatch(const Gt4LinkRecord& header, const uint8_t* payload) { stream->record(header, payload); }

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
    if (impl->worker.joinable()) impl->worker.join();
    impl->link.reset();   // asks the game to quit
    std::printf("GS: %s\n", summary().c_str());
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
    h.backend = Backend::create(options.backend, error);
    if (!h.backend) {
        error = "GS back end: " + error;
        return nullptr;
    }
    std::printf("GS: paraLLEl-GS on %s, %ux super-sampling\n", h.backend->deviceName().c_str(), options.backend.superSampling);
    Impl* raw = &h;
    h.target = std::make_unique<BackendTarget>(*h.backend, [raw](DisplayFrame& f) { raw->publish(f); });
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
        h.backend->setEventSink([raw, link](GsEventKind kind, uint64_t payload) {
            struct { uint32_t kind, id; uint64_t value; } event{static_cast<uint32_t>(kind), static_cast<uint32_t>(payload), payload};
            link->write(GT4_REC_GS_EVENT, &event, sizeof(event));
            ++raw->events;
        });
        h.worker = std::thread([raw] { raw->backend->bindToCurrentThread(); raw->runLive(); });
    } else {
        h.backend->setEventSink([raw](GsEventKind, uint64_t) { ++raw->events; });
        h.worker = options.lockstep ? std::thread([raw] { raw->backend->bindToCurrentThread(); raw->runReplayLockstep(); })
                                    : std::thread([raw] { raw->backend->bindToCurrentThread(); raw->runReplayRealTime(); });
    }
    return host;
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

bool GsHost::takeFrame(DisplayFrame& frame, uint64_t& serial) {
    std::lock_guard<std::mutex> lock(impl->frameMutex);
    if (impl->serial == serial || impl->latest.rgba.empty()) return false;
    frame = impl->latest;
    serial = impl->serial;
    return true;
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

bool GsHost::live() const { return impl->link && impl->link->running(); }
bool GsHost::finished() const { return impl->done; }
int GsHost::gameExitCode() const { return impl->link && !impl->link->running() ? impl->link->exitCode() : 0; }
std::string GsHost::deviceName() const { return impl->backend->deviceName(); }

std::string GsHost::status() const {
    const Impl& h = *impl;
    std::ostringstream out;
    if (h.link) {
        out << "game " << (h.link->running() ? "running" : "exited (" + std::to_string(h.link->exitCode()) + ")")
            << "  vblanks " << h.vsyncs.load() << "  " << h.link->bytesReceived() / (1024 * 1024) << " MiB";
    } else if (h.replay) {
        out << "replay " << h.replayFrame << "/" << h.replay->frames() << (h.done ? " (end)" : "");
    }
    {
        std::lock_guard<std::mutex> lock(h.frameMutex);
        if (!h.latest.rgba.empty()) out << "  frame " << h.latest.width << "x" << h.latest.height;
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
    const std::string timing = h.backend ? h.backend->statistics() : std::string();
    if (!timing.empty()) out << "; GPU " << timing;
    return out.str();
}

}  // namespace gt4::gs
