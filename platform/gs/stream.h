#pragma once
// The game -> host graphics stream (include/gt4/link/protocol.h): record framing and dispatch to the GS
// back end, with VIF1 and GT4's 3D renderers in between. Used both for the live link and for --replay
// captures, which are the same records concatenated. No GPU code here.
#include "gs_primitives.h"
#include "gt4/gfx.h"
#include "gt4/link/protocol.h"
#include "renderers3d.h"
#include "vif1.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gt4::ps2 {

// What the stream drives: the GS (implemented by the host over gs::Backend; tests use a recorder).
class GsTarget : public GifSink {
public:
    // Primitives from GT4's 3D renderers (VU1's path on the PS2), in order with the GIF packets.
    virtual void primitives(const GsPrimitives& p) { (void)p; }
    virtual void privileged(uint32_t offset, uint64_t value) = 0;
    // Native drawing state from the game (include/gt4/gfx.h), in order with the packets.
    virtual void state(const gfx::StateRecord& r) { (void)r; }
    // Pixels into graphics memory (`bytes` of packed pixels; fewer when the game's data was short).
    virtual void upload(const gfx::UploadRecord& u, const uint8_t* data, size_t bytes) { (void)u; (void)data; (void)bytes; }
    // A read of graphics memory (the pixels follow in readTransfer, asked for by GT4_REC_GS_READ).
    virtual void read(const gfx::ReadRecord& r) { (void)r; }
    // Primitives (`d.count` vertices).
    virtual void draw(const gfx::DrawRecord& d, const gfx::Vertex* vertices) { (void)d; (void)vertices; }
    // A VBlank: present the displayed frame. `field` is CSR.FIELD, `count` the game's VBlank counter.
    virtual void vsync(uint32_t field, uint32_t count) = 0;
    // GS -> EE transfer data (after TRXDIR = 1), `qwc` qwords into `out`.
    virtual void readTransfer(void* out, uint32_t qwc) = 0;
    // The whole 4 MiB of GS local memory (debugging: GT4_VRAM_DUMP).
    virtual void readVram(void* out) { (void)out; }
    // Replaces the whole of GS local memory (a saved state restored: GraphicsStream::restoreState).
    virtual void writeVram(const void* data) { (void)data; }
};

// Host -> game records (pad, GS events, image data, commands); the live link writes them to its ring.
using HostRecordWriter = std::function<void(uint32_t type, const void* payload, uint32_t size)>;

// The game's sound (GT4_REC_AUDIO): interleaved stereo s16 frames of one stream at `rate` Hz.
using AudioWriter = std::function<void(uint32_t stream, uint32_t rate, const int16_t* frames, size_t count)>;

// Decodes records and dispatches them. One instance per stream; not thread-safe.
class GraphicsStream {
public:
    explicit GraphicsStream(GsTarget& target);
    ~GraphicsStream();
    void setHostWriter(HostRecordWriter writer) { hostWriter = std::move(writer); }
    void setAudioWriter(AudioWriter writer) { audioWriter = std::move(writer); }

    // One whole record (header + payload).
    void record(const Gt4LinkRecord& header, const uint8_t* payload);

    // Statistics for the log.
    struct Stats {
        uint64_t records = 0, gifPackets[4] = {}, gifBytes = 0, vif1Bytes = 0, vif0Records = 0, privWrites = 0;
        uint64_t vsyncs = 0, reads = 0, logs = 0, audio = 0, unknown = 0;
        uint64_t primitiveRuns = 0, primitiveVertices = 0;   // from the 3D renderers
        uint64_t unknownPrograms = 0;   // batches started in a program image that is not GT4's (nothing drawn)
        uint64_t path3Held = 0, path3ForcedAtVsync = 0;   // PATH3 records held by MSKPATH3
        uint64_t gfxRecords = 0, badRecords = 0;   // native records in the VIF1 stream (include/gt4/gfx.h)
        uint64_t uploads = 0, uploadsWithoutData = 0, draws = 0;
    };
    const Stats& stats() const { return counters; }
    const Vif1::Stats& vifStats() const { return vif1.stats; }
    const Renderers3d::Stats& rendererStats() const { return renderers.stats; }
    std::string describe() const;
    Vif1& vif() { return vif1; }
    Renderers3d& renderers3d() { return renderers; }

    // A capture taken mid-run (GT4_HOST_RECORD from a later VBlank) starts with a GT4_REC_STATE record:
    // VIF1 and its batch buffers, the 3D renderers' constants, the GS registers last written and GS memory
    // (`vram`, 4 MiB in address order), so that its replay starts where the live run was. Taken at a VBlank.
    // Version 1 states (VU1 memories and registers, before 2026-10-30) are read too.
    std::vector<uint8_t> saveState(const uint8_t* vram) const;
    void restoreState(const uint8_t* payload, size_t size);

private:
    // PATH2 (DIRECT) packets go to the GS through this, which counts them.
    struct CountingSink final : GifSink {
        GraphicsStream* owner = nullptr;
        void gif(uint32_t path, const uint8_t* data, size_t bytes) override {
            ++owner->counters.gifPackets[path & 3];
            owner->sendGif(path, data, bytes);
        }
    };
    // The 3D renderers' output: the game's packets they pass on (as PATH1, VU1's path) and primitives.
    struct RendererSink final : PrimitiveSink {
        GraphicsStream* owner = nullptr;
        void packet(const uint8_t* gif, size_t bytes) override {
            ++owner->counters.gifPackets[1];
            owner->sendGif(1, gif, bytes);
        }
        void primitives(const GsPrimitives& p) override { owner->sendPrimitives(p); }
    };
    void runBatch(const Batch3d& batch);
    void records(const uint8_t* data, size_t bytes);
    // The native drawing state, as state records and GIF packets set it (for saveState).
    gfx::Shared nativeShared;
    gfx::Context nativeContexts[2];
    std::array<gfx::StateRecord, 2> nativeState() const;
    void applyNative(const gfx::StateRecord& r);
    // The bytes records refer to (GT4_REC_GFX_DATA), by game address, until their record takes them.
    struct RecordData {
        uint32_t source = 0;
        std::vector<uint8_t> bytes;
    };
    std::vector<RecordData> recordData;
    std::vector<gfx::Vertex> drawVertices;
    void sendPrimitives(const GsPrimitives& p);
    // Every GIF packet goes to the GS through this (GT4_VRAM_DUMP counts them).
    void sendGif(uint32_t path, const uint8_t* data, size_t bytes);
    void countPacket();
    // GT4_VRAM_DUMP=FRAME:N1,N2,...: write GS memory to vram_FRAME_N.bin before GIF packet N of that
    // frame (packets counted from 0 after the previous VSYNC; "end" = at the VSYNC). GT4_GIF_LOG=FRAME
    // prints that frame's packets (index, path, size, first registers).
    uint32_t dumpFrame = ~0u, logFrame = ~0u, frameIndex = 0, packetIndex = 0;
    std::vector<uint32_t> dumpAt;
    bool dumpAtEnd = false;
    void dumpVram(const char* tag);
    GsTarget& gs;
    CountingSink sink;
    RendererSink rendererSink;
    Vif1 vif1;
    Renderers3d renderers;
#if GT4_VU1_INTERPRETER
    // The reference tools (tools/vu1ref): GT4_VU1=ref|interp draws with them, GT4_VU1_DIFF=1 compares.
    struct Reference;
    std::unique_ptr<Reference> reference;
#endif
    HostRecordWriter hostWriter;
    AudioWriter audioWriter;
    Stats counters;
    std::vector<uint8_t> scratch;
    // PATH3 data that arrived while VIF1 had PATH3 masked (MSKPATH3): sent when it is unmasked, as the
    // GIF arbiter would, or at the next VSYNC if the mask is never lifted.
    std::vector<uint8_t> heldPath3;
    void releasePath3();
    // The GS registers last written, for saveState: general registers by address (as the GIF packets set
    // them), privileged ones by offset / 16 (0x00-0xF0; IMR in the last slot).
    static constexpr uint32_t kGsRegs = 0x64;
    uint64_t gsRegs[kGsRegs] = {}, gsRegsSet[2] = {};
    uint64_t privRegs[17] = {};
    uint32_t privSet = 0;
    struct GifShadow {
        uint64_t regs = 0;
        uint32_t left = 0;   // PACKED: qwords; REGLIST: words; IMAGE: qwords
        uint32_t flg = 0, nreg = 1, reg = 0;
    } gifShadow[4];
    uint32_t packedQ = 0x3F800000;   // Q as PACKED ST left it, for RGBAQ
    void shadowGif(uint32_t path, const uint8_t* data, size_t bytes);
    void shadowReg(uint32_t reg, uint64_t value) {
        if (reg >= kGsRegs) return;
        gsRegs[reg] = value;
        gsRegsSet[reg >> 6] |= 1ull << (reg & 63);
        bool isShared = false;
        uint32_t context = 0, group = 0;
        if (gfx::groupOfRegister(reg, isShared, context, group)) {
            if (isShared) gfx::setSharedGroup(nativeShared, group, value);
            else gfx::setContextGroup(nativeContexts[context], group, value);
        }
    }
};

// Splits a byte buffer of concatenated records (a capture file, or bytes copied out of the ring).
// Returns the bytes consumed (whole records only); calls `onRecord` for each.
size_t splitRecords(const uint8_t* data, size_t size,
                    const std::function<void(const Gt4LinkRecord&, const uint8_t*)>& onRecord);

constexpr size_t recordSpan(uint32_t payloadSize) { return (sizeof(Gt4LinkRecord) + payloadSize + 15) & ~size_t(15); }

}  // namespace gt4::ps2
