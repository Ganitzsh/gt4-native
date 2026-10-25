// The EE's privileged one-instruction asm statements, which tools/game_cc.py turns into calls
// (compat/game/gt4_prelude.h): ei/di, mfc0, the module $gp, break.
#include "kernel.h"
#include "log.h"

#include <cstdint>

using namespace gt4::rt;

namespace {
thread_local void *t_gp = reinterpret_cast<void *>(0x006DDDF0);  // the game's $gp (crt0)
}

extern "C" {

void __gt4_ee_ei(void) { kernel_ei(); }
void __gt4_ee_di(void) { kernel_di(); }

// COP0 registers the game reads: Status (12) for the interrupt-enable bits, Count (9), and the
// performance counters (25), which the renderer accumulates per frame.
unsigned int __gt4_ee_mfc0(int reg) {
    switch (reg) {
    case 12: {  // Status: EIE (bit 16) + IE (bit 0); EXL (bit 1) inside an interrupt handler
        if (kernel_in_interrupt()) return 0x10003;
        return kernel_interrupts_enabled() ? 0x10001 : 0x00001;
    }
    case 9:  // Count: the CPU clock, twice the bus clock
        return static_cast<unsigned int>(kernel_bus_cycles() * 2);
    case 25:
        return static_cast<unsigned int>(kernel_bus_cycles() * 2);
    default:
        return 0;
    }
}

void *__gt4_ee_get_gp(void) { return t_gp; }
void __gt4_ee_set_gp(void *gp) { t_gp = gp; }

void __gt4_ee_break(int code) { fatal("game code executed a break instruction (code %d)", code); }

}  // extern "C"
