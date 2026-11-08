#pragma once
// VIF1: GT4's 3D batches. The game sends them as native batch records (include/gt4/gfx.h: kBatchFrame, kBatchWrite,
// kBatchModel, kBatchRun); a model packet (kBatchModel) is the model file's own VIF codes, decoded here into the
// batch buffers, and a batch's start runs one of GT4's 3D renderers (renderers3d.cpp, natively). Written for this
// project from the public VIF documentation; the details of UNPACK filling writes, V2/V3 field order and MODE 3
// follow PCSX2 (GPL-3.0-or-later), consulted as a reference.
//
// The VIF1 stream itself (GT4_REC_VIF1: DIRECT packets for PATH2, MPG, MSKPATH3, records inside the stream) is gone
// with its record kind (PLAN.md P1): the game has not sent it since its records went native. A model packet holds
// only UNPACK and state codes; anything else in one is counted as unknown. FLUSH/FLUSHE/FLUSHA have nothing to wait for.
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gt4::ps2 {

class GifSink {
public:
    virtual ~GifSink() = default;
    virtual void gif(uint32_t path, const uint8_t* data, size_t bytes) = 0;
};

// The data the game unpacked for 3D batches, by qword from the start of the batch (UNPACK with FLG is
// relative to it). GT4 double-buffers: two batch buffers (VIF BASE and BASE + OFFSET, 0 and 435 qwords)
// take turns, and the game re-sends only what changed between two batches of the same buffer (lighting
// constants stay for a whole model), so each buffer keeps what it was last sent. It may also draw a batch
// again without sending its vertices: the second draw sees them as the first left them, decoded (packed
// positions and texture coordinates converted, colours lit), so the renderers write the decoded vertex
// data back (renderers3d.cpp, docs/VU1_PROGRAMS.md).
struct BatchBuffer {
    static constexpr uint32_t kQwords = 256;   // the batch layout ends at qword 242 (docs/VU1_PROGRAMS.md)
    uint32_t words[kQwords][4] = {};
    uint32_t vertexWrites = 0;   // writes into the vertex data (qwords 0..191) so far (renderers3d: a batch drawn again)
    // The colour slot's qwords (128..191, one bit each) that a lighting chain run on the GPU left as lit colour
    // words or as decoded normals: the VU, and the CPU renderers, write those back into the batch, the GPU does
    // not, and a later batch can read one (0x0290's "next normal" after its last vertex). An upload clears them.
    uint64_t litColours = 0, litNormals = 0;
    void uploaded(uint32_t q) {
        if (q >= 128 && q < 192) {
            const uint64_t keep = ~(1ull << (q - 128));
            litColours &= keep;
            litNormals &= keep;
        }
    }
    const uint32_t* qword(uint32_t q) const { return words[q % kQwords]; }
    uint32_t* qword(uint32_t q) { return words[q % kQwords]; }
};

// One batch: the renderer's entry (MSCAL's address, 8-byte units), the batch, and ITOP (the lighting
// chain's selector).
struct Batch3d {
    uint32_t entry = 0;
    BatchBuffer* buffer = nullptr;
    uint32_t itop = 0;
    uint32_t top = 0;   // the buffer's VU address (statistics, the reference tools)
};

class Vif1 {
public:
    Vif1();
    // A batch is complete: run its renderer (GraphicsStream).
    std::function<void(const Batch3d& batch)> onBatch;
    // Native batch records (include/gt4/gfx.h): a frame starts (the layout GT4's frame head sets: buffers at 0 and
    // 435, cycle 4/4, no row mode, mask C0C0C0C0, the first buffer next); qwords into the batch being filled (the
    // lanes in `lanes`); a model packet decoded into it with `mask`; the batch run.
    void batchFrame();
    void batchWrite(uint32_t qword, const uint32_t* words, uint32_t count, uint32_t lanes);
    void batchModel(const uint8_t* bytes, size_t size, uint32_t modelMask, uint64_t contentHash = 0);   // (hash 0: worked out here)
    void batchRun(uint32_t entry, uint32_t itop);
    void reset();

    // Whether the micro program image is GT4's (NTSC-U v1.01) or none. It comes only from a saved state now (the
    // game sends no MPG); kept for this check: the renderers are native code (renderers3d.cpp) and an unknown image
    // draws nothing.
    bool gt4Program();
    static constexpr uint32_t kProgramBytes = 16384;
    std::vector<uint8_t> program = std::vector<uint8_t>(kProgramBytes);
    void programChanged() { programKnown = false; }

    // Registers (visible for tests, debugging and saved states).
    uint32_t cl = 1, wl = 1;          // STCYCL
    uint32_t mode = 0;                // STMOD
    uint32_t mask = 0;                // STMASK
    uint32_t row[4] = {}, col[4] = {};
    uint32_t base = 0, ofst = 0, tops = 0, itops = 0;
    bool dbf = false;
    uint32_t mark = 0;
    // The two batch buffers: [0] at BASE, [1] at BASE + OFFSET.
    BatchBuffer buffers[2];
    uint32_t continueAt = 0;          // where MSCNT goes on: the end of the last renderer (set by the caller)

    struct Stats {
        uint64_t codes = 0, unpacks = 0, microprograms = 0;
        uint64_t unknownCodes = 0;
        uint64_t outsideBatch = 0;      // unpacked qwords outside both batch buffers (dropped)
        uint64_t absoluteUnpacks = 0;   // UNPACK without FLG (placed by address in a buffer)
    } stats;
    std::string lastWarning;
    uint32_t lastProgram = 0;   // byte address of the last renderer started (statistics)

#if GT4_VU1_INTERPRETER
    // The reference tools (tools/vu1ref) keep a VU1 memory model beside the batches: every qword an UNPACK
    // writes (its VU address, the lanes written). (mpg: no longer called, the stream that carried MPG is gone.)
    struct Observer {
        virtual ~Observer() = default;
        virtual void unpacked(uint32_t address, const uint32_t value[4], unsigned lanes) = 0;
        virtual void mpg(uint32_t address, const uint32_t* words, uint32_t instructions) = 0;
    };
    Observer* observer = nullptr;
#endif

    // Model packets decoded once (docs/NATIVE_V2.md, step 2): a packet's effect follows from its bytes and the VIF
    // state it starts in, so it is kept as the writes it made into the batch buffers and the state it left.
    struct ModelEffect {
        struct Write {
            uint16_t buffer, qword;   // which batch buffer (0, 1), the qword
            uint32_t lanes;
            uint32_t value[4];
        };
        std::vector<Write> writes;
        // The writes as replayed (`compact`: all in the batch buffers' range): merged per qword, those whole copied as
        // runs, the others lane by lane, and per buffer the vertex writes counted and the colour slot's qwords
        // uploaded (BatchBuffer::uploaded) in one go.
        struct Run {
            uint16_t buffer, qword, count;
            uint32_t at;   // into runWords
        };
        bool compact = false;
        std::vector<Run> runs;
        std::vector<uint32_t> runWords;
        std::vector<Write> partial;
        uint32_t vertexWrites[2] = {0, 0};
        uint64_t uploadedSlots[2] = {0, 0};
        uint32_t cl, wl, mask, mode, row[4], col[4], base, ofst, tops, itops, mark;
        bool dbf;
        uint64_t unpacks, codes;
    };
    struct ModelStats {
        uint64_t decoded = 0, replayed = 0, uncacheable = 0;
    } modelStats;

private:
    std::vector<uint32_t> pending;    // the model packet's words being decoded (batchModel)
    std::unordered_map<uint64_t, ModelEffect> modelEffects;
    ModelEffect* recording = nullptr;   // the effect being recorded (batchModel)
    bool cacheable = true;              // ... nothing in the packet but UNPACK and state codes
    size_t pos = 0;
    bool programKnown = false, programIsGt4 = false;
    bool inModel = false;   // decoding a model packet (batchModel)

    // A model packet's words, decoded (batchModel).
    void feed(const uint8_t* bytes, size_t size);
    // Returns false when the command at `pos` needs more words than are available.
    bool step();
    void start(uint32_t address);
    void unpack(uint32_t code, const uint32_t* source);
    static uint32_t unpackWords(uint32_t code, uint32_t cl, uint32_t wl);
};

}  // namespace gt4::ps2
