#pragma once
// A second VU1 interpreter, used only as a reference to check vu1.cpp (GT4_VU1_DIFF, see vif1.cpp).
// Adapted from the VU micro interpreter of ps2-web-recomp (runtime/src/vu_micro.cpp, include/ps2/fpu.hpp,
// vu.hpp, vu_ops.hpp), MIT licence, Copyright (c) 2026 Samir Nuri; see THIRD_PARTY.md. Its model differs from
// vu1.cpp on purpose: no FMAC/Q/P latencies (results are written at once), exact integer float arithmetic
// with truncation, flags visible to the lower slot 4 instructions later.
#include "vu1.h"

#include <cstdint>
#include <vector>

namespace gt4::ps2 {

struct Vu1Ref {
    VuVector vf[32];
    VuVector acc;
    uint16_t vi[16];
    uint32_t q = 0, p = 0, i = 0, r = 0, mac = 0, status = 0, clip = 0, tpc = 0;
    uint32_t top = 0, itop = 0;
    uint8_t micro[Vu1::kMicroBytes];
    uint8_t data[Vu1::kDataBytes];
    std::vector<uint8_t> kicks;   // every XGKICK packet, concatenated
    GifSink* sink = nullptr;      // also sent each packet when set
    std::FILE* trace = nullptr;   // debugging: one line per instruction pair
    bool runaway = false;

    // Copies the architectural state, micro and data memory of `vu`.
    void load(const Vu1& vu);
    // Writes the state and data memory back to `vu` (GT4_VU1=ref: this interpreter drives the frame).
    void store(Vu1& vu) const;
    void run(uint32_t pc);
};

}  // namespace gt4::ps2
