#pragma once
// VU1: the PS2's vector unit 1 in micro mode, interpreted on the CPU.
//
// DEVELOPMENT SCAFFOLD (CMake option GT4_VU1_INTERPRETER, on by default for development, not for
// releases): GT4's 3D runs through VU1 microprograms the game uploads with VIF1 MPG; until those are
// translated natively, this interpreter runs them. Written for this project from the public VU
// documentation; behavioural details (flag and Q/P pipeline latencies, the integer-branch read of the
// previous value, the result of a VF write by both slots) follow PCSX2's interpreter (GPL-3.0-or-later),
// which was consulted as a reference. See docs/GS_BACKEND.md.
//
// Model: one instruction pair per cycle; FMAC results ready 4 cycles after issue (dependent VF reads stall;
// ACC is forwarded, so MULA/MADDA chains never stall: a stall there delays the flags GT4's culling reads);
// MAC/status/clip flags become visible to the lower-slot flag instructions 4 cycles after the FMAC
// instruction that produced them; Q (DIV 7, SQRT 7, RSQRT 13 cycles) and P (EFU latencies) are written
// when their unit finishes, WAITQ/WAITP and a second DIV/EFU op wait for it; floats follow the VU's
// non-IEEE rules (no denormals, infinities or NaNs: inputs clamp, results saturate to +-max with the O
// flag, underflow gives a signed zero with U), arithmetic rounds toward zero (computed in double
// precision, then truncated; independent of the host FPU mode). XGKICK sends the GIF
// packet at once (PATH1). The program ends after the instruction following the one with the E bit.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace gt4::ps2 {

// Where VU1 (XGKICK, PATH1) and VIF1 (DIRECT, PATH2) send GIF data.
class GifSink {
public:
    virtual ~GifSink() = default;
    virtual void gif(uint32_t path, const uint8_t* data, size_t bytes) = 0;
};

union VuVector {
    float f[4];
    uint32_t u[4];
    int32_t s[4];
};

// Debugging: after each traced instruction pair, the VF registers listed in GT4_VU1_TRACE_REGS (comma-
// separated numbers, 32 = ACC) as hex words.
inline void traceRegs(std::FILE* trace, const VuVector* vf, const VuVector& acc) {
    static const char* list = std::getenv("GT4_VU1_TRACE_REGS");
    if (!list) return;
    for (const char* p = list; *p;) {
        char* end = nullptr;
        const long r = std::strtol(p, &end, 10);
        if (end == p) break;
        const VuVector& v = r == 32 ? acc : vf[r & 31];
        std::fprintf(trace, "   %s%ld %08X %08X %08X %08X\n", r == 32 ? "acc" : "vf", r == 32 ? 0L : r, v.u[0], v.u[1], v.u[2], v.u[3]);
        p = *end == ',' ? end + 1 : end;
    }
}

class Vu1 {
public:
    static constexpr uint32_t kMicroBytes = 16384;   // 2048 instruction pairs
    static constexpr uint32_t kDataBytes = 16384;    // 1024 qwords

    Vu1();
    void reset();
    void setGifSink(GifSink* sink) { gifSink = sink; }
    GifSink* sink() const { return gifSink; }

    // Runs from instruction `pc` (8-byte units) until the program ends (E bit). Returns the cycles taken.
    // A program that never ends is stopped after `maxCycles` (reported by `runaway()`).
    uint64_t run(uint32_t pc, uint64_t maxCycles = 50'000'000);
    bool runaway() const { return stoppedRunaway; }
    uint32_t pc() const { return tpc; }   // where MSCNT continues
    void setPc(uint32_t pc) { tpc = pc & 0x7FF; }   // a saved state restored (GraphicsStream::restoreState)
    // XGKICK: sends the GIF packet at `qword` (PATH1); used by the native microprograms (vu1_native.cpp).
    void kick(uint32_t qword) { xgkick(qword); }

    // VIF1 double-buffer pointers (XTOP / XITOP read them).
    uint32_t top = 0, itop = 0;

    uint8_t micro[kMicroBytes];
    uint8_t data[kDataBytes];

    // Architectural state (visible for tests and debugging).
    VuVector vf[32];
    VuVector acc;
    uint16_t vi[16];
    uint32_t i = 0, q = 0, p = 0, r = 0;
    uint32_t mac = 0, status = 0, clip = 0;   // as the lower-slot flag instructions see them

    uint64_t instructions = 0;   // executed pairs since reset (statistics)
    std::FILE* trace = nullptr;  // debugging: one line per instruction pair
    uint8_t* coverage = nullptr; // statistics: 2048 bytes, set to 1 for each instruction executed

private:
    struct FmacEntry {
        uint64_t ready;
        uint32_t mac, status, clip;
        bool statusSet, clipSet;   // FSSET / CLIP / FCSET entries
    };
    GifSink* gifSink = nullptr;
    uint32_t tpc = 0;
    uint64_t cycle = 0;
    bool stoppedRunaway = false;
    // Internal (latest) flags; the pipeline makes them visible later.
    uint32_t macInternal = 0, statusInternal = 0, clipInternal = 0;
    FmacEntry fmac[8];
    uint32_t fmacCount = 0;
    uint64_t vfReady[33][4];   // cycle each field becomes readable; [32] is ACC
    struct { bool active; uint64_t ready; uint32_t value, flags; } fdiv{}, efu{};
    // Integer-branch quirk: a branch right after the instruction that wrote its VI register reads the
    // previous value.
    int viBackupCycles = 0;
    uint32_t viBackupReg = 0;
    uint16_t viBackupValue = 0;
    int branchPending = 0;
    uint32_t branchTarget = 0;
    bool delayBranch = false;
    uint32_t delayTarget = 0;

    void flushPipes();
    void flushAll();
    void stallOn(uint32_t reg, uint32_t mask);
    void writeVi(uint32_t reg, uint16_t value);
    uint16_t branchVi(uint32_t reg) const;
    void setBranch(uint32_t target);
    void xgkick(uint32_t address);
    uint32_t* mem(uint32_t qword) { return reinterpret_cast<uint32_t*>(data + (qword & 0x3FF) * 16); }
    friend struct VuExec;
    friend struct Vu1Ref;
    friend class Vu1Native;
};

}  // namespace gt4::ps2
