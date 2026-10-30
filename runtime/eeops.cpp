// The EE's privileged asm instructions, which tools/game_cc.py turns into calls
// (compat/game/gt4_prelude.h, gt4_asm.h): ei/di, mfc0/mtc0, the module $gp, break, syscall.
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

// The same as plain C, for native sources (game_src/) written without asm (compat/game/gt4_prelude.h).
// gt4_interrupts_enabled is Status.EIE as __gt4_ee_mfc0(12) gives it: also set inside an interrupt handler.
int gt4_interrupts_enabled(void) { return kernel_in_interrupt() || kernel_interrupts_enabled(); }
int gt4_in_interrupt(void) { return kernel_in_interrupt(); }
void gt4_interrupts_disable(void) { kernel_di(); }
void gt4_interrupts_enable(void) { kernel_ei(); }
unsigned int gt4_cycle_count(void) { return static_cast<unsigned int>(kernel_bus_cycles() * 2); }
void *gt4_get_gp(void) { return t_gp; }
void gt4_set_gp(void *gp) { t_gp = gp; }

void __gt4_ee_break(int code) { fatal("game code executed a break instruction (code %d)", code); }

// mtc0 (tools/game_toolchain/asm_translate.py): the performance counter control (25), Wired (6),
// and other COP0 writes have nothing to drive natively.
void __gt4_ee_mtc0(int reg, unsigned int value) {
    (void)reg;
    (void)value;
}

int gt4_sys_GetThreadId();

// An inline `syscall` (asm_translate.py: $v1 = number, $a0-$a3 = arguments, result in $v0). The
// syscall stubs of the image go through kernel_install_syscalls; this is for the few that the
// game's C code issues itself (iWakeupThread/iSuspendThread in thread.o: iGetThreadId).
int __gt4_ee_syscall(int number, unsigned int a0, unsigned int a1, unsigned int a2, unsigned int a3) {
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    switch (number) {
    case 0x2F:
    case -0x2F:
        return gt4_sys_GetThreadId();
    default:
        fatal("game code issued syscall %d (0x%x) inline; not implemented", number, number & 0xFFFF);
    }
}

}  // extern "C"
