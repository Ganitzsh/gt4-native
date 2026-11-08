#pragma once
// The game -> host graphics stream (include/gt4/link/protocol.h): record framing and dispatch to the GS
// back end, with VIF1 and GT4's 3D renderers in between. Used both for the live link and for --replay
// captures, which are the same records concatenated. No GPU code here.
#include "gs_primitives.h"
#include "gt4/gfx.h"
#include "gt4/link/protocol.h"
#include "renderers3d.h"
#include "scene_decoder.h"
#include "vif1.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gt4::ps2 {

// What the stream drives: the GS (implemented by the host over gs::Backend; tests use a recorder).
class GsTarget : public GifSink {
public:
    // Primitives from GT4's 3D renderers (VU1's path on the PS2), in order with the GIF packets.
    virtual void primitives(const GsPrimitives& p) { (void)p; }
    // The display from this VBlank on (include/gt4/gfx.h DisplayRecord).
    virtual void display(const gfx::DisplayRecord& d) = 0;
    // Native drawing state from the game (include/gt4/gfx.h), in order with the packets.
    virtual void state(const gfx::StateRecord& r) { (void)r; }
    // Pixels into graphics memory (`bytes` of packed pixels; fewer when the game's data was short).
    virtual void upload(const gfx::UploadRecord& u, const uint8_t* data, size_t bytes) { (void)u; (void)data; (void)bytes; }
    // A read of graphics memory (the pixels follow in readTransfer, asked for by GT4_REC_GS_READ).
    virtual void read(const gfx::ReadRecord& r) { (void)r; }
    // Primitives (`d.count` vertices).
    virtual void draw(const gfx::DrawRecord& d, const gfx::Vertex* vertices) { (void)d; (void)vertices; }
    // A texture set (docs/NATIVE_V2.md): gfx::TexSetRecord, its uploads and textures, then (when sent) the bytes.
    virtual void texSet(const uint8_t* payload, size_t bytes) { (void)payload; (void)bytes; }
    // A post effect as a pass (docs/NATIVE_V2.md, step 3).
    virtual void effect(const gfx::EffectRecord& r) { (void)r; }
    // A scene record (gfx::kScene: gfx::scene::Prefix, then that kind's struct), in order with the batches.
    // Nothing by default (docs/modern/CONTRACTS.md (a): the scene decoder takes them, ticket A10).
    virtual void scene(const uint8_t* payload, size_t bytes) { (void)payload; (void)bytes; }
    // The scene frame a FrameEnd just closed (after that record's scene()), for the target to take (move from it) and
    // put on the FrameList being built (CONTRACTS.md a.8). Left in the decoder (takeSceneFrame) by default.
    virtual void sceneFrame(std::unique_ptr<scene::SceneFrame>& frame) { (void)frame; }
    // A view's package at its ViewEnd (CONTRACTS.md a.8), for the FrameList being built (a partial one may substitute it).
    // Made only for a target that wants them.
    virtual bool wantsSceneViews() const { return false; }
    virtual void sceneView(std::shared_ptr<const scene::SceneView> view) { (void)view; }
    // A 3D batch for the GPU (docs/NATIVE_V2.md, step 2; PrimitiveSink::model), when the target takes them.
    virtual bool models() const { return false; }
    virtual void model(const gfx::ModelJob& job, const uint32_t* words) { (void)job; (void)words; }
    // A VBlank: present the displayed frame. `field` is CSR.FIELD, `count` the game's VBlank counter.
    virtual void vsync(uint32_t field, uint32_t count) = 0;
    // GS -> EE transfer data (after TRXDIR = 1), `qwc` qwords into `out`.
    virtual void readTransfer(void* out, uint32_t qwc) = 0;
    // A read the game does not wait for (gfx::kReadAsync): true when the target answers it itself, later (GS_IMAGE
    // `id`, `qwc` qwords, written by its own means); false: readTransfer now, as for any other.
    virtual bool readLater(uint32_t id, uint32_t qwc) { (void)id; (void)qwc; return false; }
    virtual bool readsLater() const { return false; }   // whether readLater can return true
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
    // GT4_REC_RUMBLE: a pad's motors changed (port, small 0 / 1, large 0-255), on the stream's thread.
    void setRumbleWriter(std::function<void(uint32_t port, uint32_t small, uint32_t large)> writer) {
        rumbleWriter = std::move(writer);
    }
    // GT4_REC_UI and GT4_REC_SETTINGS_SET (the settings pages, include/gt4/ui.h): the payload, on the stream's thread.
    void setUiWriter(std::function<void(const uint8_t*, size_t)> writer) { uiWriter = std::move(writer); }
    void setSettingsSetWriter(std::function<void(const uint8_t*, size_t)> writer) {
        settingsSetWriter = std::move(writer);
    }

    // One whole record (header + payload).
    void record(const Gt4LinkRecord& header, const uint8_t* payload);

    // Statistics for the log.
    struct Stats {
        uint64_t records = 0, gifPackets[4] = {}, gifBytes = 0, vif1Bytes = 0, vif0Records = 0, displays = 0;
        uint64_t vsyncs = 0, reads = 0, logs = 0, audio = 0, unknown = 0;
        uint64_t ui = 0;   // the settings pages' records (GT4_REC_UI, GT4_REC_SETTINGS_SET)
        uint64_t primitiveRuns = 0, primitiveVertices = 0;   // from the 3D renderers
        uint64_t texSets = 0;   // native texture set records (docs/NATIVE_V2.md)
        uint64_t modelBatches = 0;   // 3D batches drawn on the GPU (docs/NATIVE_V2.md, step 2)
        uint64_t unknownPrograms = 0;   // batches started in a program image that is not GT4's (nothing drawn)
        uint64_t path3Held = 0, path3ForcedAtVsync = 0;   // PATH3 records held by MSKPATH3
        uint64_t gfxRecords = 0, badRecords = 0;   // native records in the VIF1 stream (include/gt4/gfx.h)
        uint64_t sceneRecords = 0;   // of them gfx::kScene (0 while the game's kOptionSceneRecords is off)
        uint64_t uploads = 0, uploadsWithoutData = 0, draws = 0;
    };
    const Stats& stats() const { return counters; }
    // The scene records' decoder (ticket A10): its counters, and the frame its last FrameEnd closed.
    const scene::SceneDecoder& sceneDecoder() const { return sceneDecoder_; }
    std::unique_ptr<scene::SceneFrame> takeSceneFrame() { return sceneDecoder_.takeFrame(); }
    // CONTRACTS.md a.5.1's checks at each VSync: a scene frame still open (a VSync inside a kicked list), and more than one
    // frame ended since the last VSync (two kicks' frames in one FrameList)
    uint64_t sceneVsyncsInFrame() const { return sceneVsyncsInFrame_; }
    uint64_t sceneListsWithFrames() const { return sceneListsWithFrames_; }
    void finishScene() { sceneDecoder_.finish(); }   // the stream ended (before the summary): framesTruncated
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
            owner->logDirect(data, bytes);
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
        bool models() const override { return owner->gs.models(); }
        void model(const gfx::ModelJob& job, const uint32_t* words) override {
            ++owner->counters.modelBatches;
            if (!owner->sceneDecoder_.seen()) {   // no scene records (the option off): the job as it always was
                owner->gs.model(job, words);
                return;
            }
            gfx::ModelJob tagged = job;   // pad[0] = its instance's position + 1 (CONTRACTS.md a.5 3), checked against it
            owner->sceneDecoder_.job(tagged);
            owner->gs.model(tagged, words);
        }
    };
    void runBatch(const Batch3d& batch);
    // GT4_DIRECT_LOG=N (debugging): the first N GIF packets that still come through VIF1 DIRECT, with the VBlank
    // and the packet's first tag and registers (what game code still writes as GS packets).
    void logDirect(const uint8_t* data, size_t bytes);
    uint32_t lastVblank = 0;
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
        bool reference = false;   // a set upload's content the host keeps (gfx::kDataReference): `hash`, `size` bytes
        uint64_t hash = 0;
        uint32_t size = 0;
    };
    std::vector<RecordData> recordData;
    // The texture sets' upload contents (gfx::ContentFifo of kUploadsKept, as the runtime keeps them): bytes by hash.
    gfx::ContentFifo uploadHashes{gfx::kUploadsKept};
    std::unordered_map<uint64_t, std::vector<uint8_t>> uploadBytes;
    // A reference to a content not kept (never in a live run unless the two ends diverged): the set is not stored, and
    // the game is asked to send every content again (contentsLost; the host's GT4_REC_HOST_CMD 5 until its mark).
    bool contentsLost = false, contentsAsked = false;
    // A capture that starts mid-run, from its saved state to its first VBlank: the recorder's restoration (the kept
    // upload contents, then the front end's sets with their bytes, then the model contents). The sets' uploads do
    // not go into uploadHashes there: the kept contents alone rebuild it in the live order, and a set the front end
    // still held but whose content the runtime had dropped would add an entry the runtime does not have, so the
    // two ends' FIFOs part and a later reference names a content this end evicted (garbage textures in replays).
    bool restoring = false;
public:
    // GT4_TEST_FORGET_UPLOADS=VBLANK (testing the recovery): the kept upload bytes dropped, as if the ends diverged.
    void forgetUploadsForTest() { uploadBytes.clear(); }
    bool takeContentsLost() {
        if (!contentsLost || contentsAsked) return false;
        contentsAsked = true;
        return true;
    }
private:
    void keepUpload(uint64_t hash, std::vector<uint8_t>&& bytes) {
        if (uploadHashes.add(hash, [&](uint64_t gone) { uploadBytes.erase(gone); })) uploadBytes[hash] = std::move(bytes);
    }
    // The model packets' contents (gfx::ContentFifo, as the runtime keeps them): bytes by hash.
    gfx::ContentFifo modelHashes{gfx::kModelsKept};
    std::unordered_map<uint64_t, std::vector<uint8_t>> modelBytes;
public:
    // Every set upload content kept, oldest first (a recording that starts mid-run).
    void exportUploads(const std::function<void(const std::vector<uint8_t>& bytes)>& out) const {
        for (const uint64_t h : uploadHashes.all()) {
            auto it = uploadBytes.find(h);
            if (it != uploadBytes.end()) out(it->second);
        }
    }
    // The record data received and not yet taken by its record, oldest first (a recording that starts mid-run writes
    // them after its restoration: the game sends a set's upload bytes ahead of the set's record, and a record that
    // came after the recording's start found no data in the replay). `field` is GT4_REC_GFX_DATA's u32 bytes field.
    void exportPendingData(const std::function<void(uint32_t source, uint32_t field, const std::vector<uint8_t>& payload)>& out) const {
        for (const RecordData& d : recordData) {
            if (d.reference) {   // the content's hash, then its bytes when the game sent them too (GT4_VERIFY_UPLOADS)
                std::vector<uint8_t> payload(8 + d.bytes.size());
                std::memcpy(payload.data(), &d.hash, 8);
                if (!d.bytes.empty()) std::memcpy(payload.data() + 8, d.bytes.data(), d.bytes.size());
                out(d.source, d.size | gfx::kDataReference, payload);
            } else {
                out(d.source, static_cast<uint32_t>(d.bytes.size()), d.bytes);
            }
        }
    }
    // Every model packet content kept, oldest first (a recording that starts mid-run).
    void exportModels(const std::function<void(uint64_t hash, const std::vector<uint8_t>& bytes)>& out) const {
        for (const uint64_t h : modelHashes.all()) {
            auto it = modelBytes.find(h);
            if (it != modelBytes.end()) out(h, it->second);
        }
    }
private:
    std::vector<uint8_t> texSetBlob;   // a texture set record with its bytes (kTexSet)
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
    std::deque<bool> readsAsync;   // the read records not yet asked for by GT4_REC_GS_READ: gfx::kReadAsync each
    AudioWriter audioWriter;
    std::function<void(uint32_t, uint32_t, uint32_t)> rumbleWriter;
    std::function<void(const uint8_t*, size_t)> uiWriter, settingsSetWriter;
    Stats counters;
    scene::SceneDecoder sceneDecoder_;   // the kScene records (docs/modern/CONTRACTS.md a.5, a.7; ticket A10)
    // a.5.1's checks: VSyncs that came while a scene frame was open (a list split across FrameLists), FrameLists that
    // received more than one frame's end (two kicks in one); the decoder's frame total at the last VSync
    uint64_t sceneVsyncsInFrame_ = 0, sceneListsWithFrames_ = 0, sceneFramesAtVsync_ = 0;
    std::vector<uint8_t> scratch;
    // PATH3 data that arrived while VIF1 had PATH3 masked (MSKPATH3): sent when it is unmasked, as the
    // GIF arbiter would, or at the next VSYNC if the mask is never lifted.
    std::vector<uint8_t> heldPath3;
    void releasePath3();
    // The GS registers last written, for saveState: general registers by address (as the GIF packets set
    // them); the display last set.
    static constexpr uint32_t kGsRegs = 0x64;
    uint64_t gsRegs[kGsRegs] = {}, gsRegsSet[2] = {};
    gfx::DisplayRecord displayState;
    bool displaySet = false;
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
