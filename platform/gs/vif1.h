#pragma once
// VIF1: decodes the VIF1 stream the game's DMA would feed (VIF codes and their data) into VU1 memory,
// VU1 microprogram starts and GIF PATH2 transfers (DIRECT / DIRECTHL).
//
// Part of the GT4_VU1_INTERPRETER development scaffold (docs/GS_BACKEND.md). Written for this project
// from the public VIF documentation; the details of UNPACK filling writes, V2/V3 field order, the
// double-buffer swap on MSCAL/MSCNT and MODE 3 follow PCSX2 (GPL-3.0-or-later), consulted as a reference.
//
// The stream is a sequence of 32-bit words. A command and its data may be split across calls; the
// remainder is kept until the rest arrives. VU1 runs synchronously: MSCAL/MSCALF/MSCNT execute the
// microprogram to its end before the next code is decoded, so FLUSH/FLUSHE/FLUSHA have nothing to wait for.
#include "vu1.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gt4::ps2 {

class Vif1 {
public:
    explicit Vif1(Vu1& vu);
    void setGifSink(GifSink* sink) { gifSink = sink; }
    // Called when MSKPATH3 changes the PATH3 mask (the stream holds PATH3 packets while it is set).
    std::function<void(bool masked)> onPath3Mask;
    // Feeds stream words (the byte count is rounded down to whole words).
    void feed(const uint8_t* bytes, size_t size);
    void reset();

    // Registers (visible for tests and debugging).
    uint32_t cl = 1, wl = 1;          // STCYCL
    uint32_t mode = 0;                // STMOD
    uint32_t mask = 0;                // STMASK
    uint32_t row[4] = {}, col[4] = {};
    uint32_t base = 0, ofst = 0, tops = 0, itops = 0;
    bool dbf = false;
    uint32_t mark = 0;
    bool path3Masked = false;         // MSKPATH3

    struct Stats {
        uint64_t codes = 0, unpacks = 0, microprograms = 0, vuCycles = 0, directQwords = 0, mpgInstructions = 0;
        uint64_t runaways = 0, unknownCodes = 0;
    } stats;
    std::string lastWarning;
    uint32_t lastProgram = 0;   // byte address of the last microprogram started (statistics)

private:
    Vu1& vu;
    GifSink* gifSink = nullptr;
    std::vector<uint32_t> pending;    // words not yet consumed
    size_t pos = 0;

    // Returns false when the command at `pos` needs more words than are available.
    bool step();
    void execute(uint32_t address);
    void unpack(uint32_t code, const uint32_t* source);
    static uint32_t unpackWords(uint32_t code, uint32_t cl, uint32_t wl);
};

}  // namespace gt4::ps2
