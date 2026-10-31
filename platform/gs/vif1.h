#pragma once
// VIF1: decodes the VIF1 stream of the game's DMA chains into what it carries: GIF packets for PATH2 (DIRECT,
// DIRECTHL) and GT4's 3D batches, the data the game unpacks for one of its 3D renderers followed by the
// start of that renderer (MSCAL), which renderers3d.cpp runs natively. Written for this project from the
// public VIF documentation; the details of UNPACK filling writes, V2/V3 field order and MODE 3 follow PCSX2
// (GPL-3.0-or-later), consulted as a reference.
//
// The stream is a sequence of 32-bit words. A command and its data may be split across calls; the
// remainder is kept until the rest arrives. A batch's renderer runs when its MSCAL is decoded, so
// FLUSH/FLUSHE/FLUSHA have nothing to wait for.
#include <cstdint>
#include <functional>
#include <string>
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
    void setGifSink(GifSink* sink) { gifSink = sink; }
    // A batch is complete: run its renderer (GraphicsStream).
    std::function<void(const Batch3d& batch)> onBatch;
    // Called when MSKPATH3 changes the PATH3 mask (the stream holds PATH3 packets while it is set).
    std::function<void(bool masked)> onPath3Mask;
    // Native records the game placed in the stream (include/gt4/gfx.h kStreamCode): whole qwords of them.
    std::function<void(const uint8_t* records, size_t bytes)> onRecords;
    // Feeds stream words (the byte count is rounded down to whole words).
    void feed(const uint8_t* bytes, size_t size);
    // Native batch records (include/gt4/gfx.h): a frame starts (the layout GT4's frame head sets: buffers at 0 and
    // 435, cycle 4/4, no row mode, mask C0C0C0C0, the first buffer next); qwords into the batch being filled (the
    // lanes in `lanes`); a model packet decoded into it with `mask`; the batch run.
    void batchFrame();
    void batchWrite(uint32_t qword, const uint32_t* words, uint32_t count, uint32_t lanes);
    void batchModel(const uint8_t* bytes, size_t size, uint32_t modelMask);
    void batchRun(uint32_t entry, uint32_t itop);
    void reset();

    // Whether the micro program image the game uploaded (MPG) is GT4's (NTSC-U v1.01). The image is kept
    // only for this check: the renderers are native code (renderers3d.cpp) and an unknown image draws nothing.
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
    bool path3Masked = false;         // MSKPATH3
    // The two batch buffers: [0] at BASE, [1] at BASE + OFFSET.
    BatchBuffer buffers[2];
    uint32_t continueAt = 0;          // where MSCNT goes on: the end of the last renderer (set by the caller)

    struct Stats {
        uint64_t codes = 0, unpacks = 0, microprograms = 0, directQwords = 0, mpgInstructions = 0, recordQwords = 0;
        uint64_t unknownCodes = 0;
        uint64_t outsideBatch = 0;      // unpacked qwords outside both batch buffers (dropped)
        uint64_t absoluteUnpacks = 0;   // UNPACK without FLG (placed by address in a buffer)
        uint64_t streamCodes[128] = {};  // codes by command in the stream itself (not in model packets)
    } stats;
    std::string lastWarning;
    uint32_t lastProgram = 0;   // byte address of the last renderer started (statistics)

#if GT4_VU1_INTERPRETER
    // The reference tools (tools/vu1ref) keep a VU1 memory model beside the batches: every qword an UNPACK
    // writes (its VU address, the lanes written), every MPG.
    struct Observer {
        virtual ~Observer() = default;
        virtual void unpacked(uint32_t address, const uint32_t value[4], unsigned lanes) = 0;
        virtual void mpg(uint32_t address, const uint32_t* words, uint32_t instructions) = 0;
    };
    Observer* observer = nullptr;
#endif

private:
    GifSink* gifSink = nullptr;
    std::vector<uint32_t> pending;    // words not yet consumed
    size_t pos = 0;
    bool programKnown = false, programIsGt4 = false;
    bool inModel = false;   // decoding a model packet (batchModel)

    // Returns false when the command at `pos` needs more words than are available.
    bool step();
    void start(uint32_t address);
    void unpack(uint32_t code, const uint32_t* source);
    static uint32_t unpackWords(uint32_t code, uint32_t cl, uint32_t wl);
};

}  // namespace gt4::ps2
