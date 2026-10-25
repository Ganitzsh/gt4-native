#pragma once
// The game -> host graphics stream (include/gt4/link/protocol.h): record framing and dispatch to the GS
// back end, with VIF1 (and VU1, GT4_VU1_INTERPRETER) in between. Used both for the live link and for
// --replay captures, which are the same records concatenated. No GPU code here.
#include "gt4/link/protocol.h"
#include "vif1.h"
#include "vu1.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gt4::ps2 {

// What the stream drives: the GS (implemented by the host over gs::Backend; tests use a recorder).
class GsTarget : public GifSink {
public:
    virtual void privileged(uint32_t offset, uint64_t value) = 0;
    // A VBlank: present the displayed frame. `field` is CSR.FIELD, `count` the game's VBlank counter.
    virtual void vsync(uint32_t field, uint32_t count) = 0;
    // GS -> EE transfer data (after TRXDIR = 1), `qwc` qwords into `out`.
    virtual void readTransfer(void* out, uint32_t qwc) = 0;
};

// Host -> game records (pad, GS events, image data, commands); the live link writes them to its ring.
using HostRecordWriter = std::function<void(uint32_t type, const void* payload, uint32_t size)>;

// Decodes records and dispatches them. One instance per stream; not thread-safe.
class GraphicsStream {
public:
    explicit GraphicsStream(GsTarget& target);
    void setHostWriter(HostRecordWriter writer) { hostWriter = std::move(writer); }

    // One whole record (header + payload).
    void record(const Gt4LinkRecord& header, const uint8_t* payload);

    // Statistics for the log.
    struct Stats {
        uint64_t records = 0, gifPackets[4] = {}, gifBytes = 0, vif1Bytes = 0, vif0Records = 0, privWrites = 0;
        uint64_t vsyncs = 0, reads = 0, logs = 0, audio = 0, unknown = 0;
    };
    const Stats& stats() const { return counters; }
    const Vif1::Stats& vifStats() const { return vif1.stats; }
    std::string describe() const;
    Vu1& vu() { return unit; }
    Vif1& vif() { return vif1; }

private:
    // PATH1 (XGKICK) and PATH2 (DIRECT) packets go to the GS through this, which counts them.
    struct CountingSink final : GifSink {
        GraphicsStream* owner = nullptr;
        void gif(uint32_t path, const uint8_t* data, size_t bytes) override {
            ++owner->counters.gifPackets[path & 3];
            owner->gs.gif(path, data, bytes);
        }
    };
    GsTarget& gs;
    CountingSink sink;
    Vu1 unit;
    Vif1 vif1;
    HostRecordWriter hostWriter;
    Stats counters;
    std::vector<uint8_t> scratch;
};

// Splits a byte buffer of concatenated records (a capture file, or bytes copied out of the ring).
// Returns the bytes consumed (whole records only); calls `onRecord` for each.
size_t splitRecords(const uint8_t* data, size_t size,
                    const std::function<void(const Gt4LinkRecord&, const uint8_t*)>& onRecord);

constexpr size_t recordSpan(uint32_t payloadSize) { return (sizeof(Gt4LinkRecord) + payloadSize + 15) & ~size_t(15); }

}  // namespace gt4::ps2
