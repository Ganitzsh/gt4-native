#pragma once
// The GS's privileged (display) registers from a display record (PLAN.md C12): the values a PCSX2 GS dump's 8 KiB
// privileged block holds (gsdump_writer.h), and the GS reference's display (tests). From GS register offsets
// 0x12000000 on: PMODE 0x0000, SMODE2 0x0020, DISPFB1 / DISPLAY1 0x0070 / 0x0080, DISPFB2 / DISPLAY2 0x0090 / 0x00A0,
// BGCOLOR 0x00E0; CSR 0x1000.
#include "gt4/gfx.h"

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace gt4::gs {

// A display record as the GS's display registers (offset, value).
inline std::vector<std::pair<uint32_t, uint64_t>> displayRegisters(const gfx::DisplayRecord& d) {
    std::vector<std::pair<uint32_t, uint64_t>> out;
    out.emplace_back(0x00, uint64_t(d.circuit[0].enabled) | uint64_t(d.circuit[1].enabled) << 1 | 1ull << 2 |
                               uint64_t(d.blendFixed) << 5 | uint64_t(d.alphaFromCircuit2) << 6 | uint64_t(d.blendBackground) << 7 |
                               uint64_t(d.alpha) << 8);
    out.emplace_back(0x20, uint64_t(d.interlaced) | uint64_t(d.fieldMode) << 1);
    for (int k = 0; k < 2; ++k) {
        const gfx::DisplayCircuit& c = d.circuit[k];
        out.emplace_back(0x70 + 0x20 * k, uint64_t(c.base / 32) | uint64_t(c.width) << 9 | uint64_t(c.format) << 15 |
                                              uint64_t(c.x) << 32 | uint64_t(c.y) << 43);
        out.emplace_back(0x80 + 0x20 * k, uint64_t(c.left) | uint64_t(c.top) << 12 | uint64_t(c.magH - 1) << 23 |
                                              uint64_t(c.magV - 1) << 27 | uint64_t(c.clocks - 1) << 32 | uint64_t(c.lines - 1) << 44);
    }
    out.emplace_back(0xE0, d.background);
    return out;
}

// The 8 KiB privileged block (kPrivilegedBytes) for a display record and the field a VSync ends: the record's registers
// at their offsets, and the ones the display record doesn't carry at GT4's NTSC values as PCSX2's own dumps of GT4 hold
// them (lane C: k001 and t50 alike), since PCSX2's CRTC takes the video mode and its timings from SMODE1 and the sync
// registers: SMODE1, SRFSH, SYNCH1, SYNCH2, SYNCV, IMR, and CSR (ID 0x55, REV 0x1B, the FIFO empty, FIELD bit 13).
inline void privilegedBlock(const gfx::DisplayRecord& d, uint32_t field, uint8_t* block8k) {
    std::memset(block8k, 0, 8192);
    for (const auto& [offset, value] : displayRegisters(d)) std::memcpy(block8k + offset, &value, 8);
    const std::pair<uint32_t, uint64_t> gt4Ntsc[] = {{0x0010, 0x0000000740814504ull},   // SMODE1
                                                      {0x0030, 0x8ull},                   // SRFSH
                                                      {0x0040, 0x0007F5B61F06F040ull},    // SYNCH1
                                                      {0x0050, 0x33A4D8ull},              // SYNCH2
                                                      {0x0060, 0x00C7800601A01801ull},    // SYNCV
                                                      {0x1010, 0x7D00ull},                // IMR
                                                      {0x1000, 0x551B4000ull | uint64_t(field & 1) << 13}};   // CSR
    for (const auto& [offset, value] : gt4Ntsc) std::memcpy(block8k + offset, &value, 8);
}

}  // namespace gt4::gs
