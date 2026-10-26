// Replacements of Sony SDK functions linked in the image (config/sdk_functions.tsv in gt4-decomp):
// a function named gt4_replace_<SDK name> takes over that function's address (tools/game_link.py).
#include "kernel.h"
#include "log.h"

#include <cstdint>

using namespace gt4::rt;

// diei.o: DIntr/EIntr disable/enable interrupts (di/ei on the EE) and return the previous state.
extern "C" int gt4_replace_DIntr() { return kernel_di(); }
extern "C" int gt4_replace_func_005B72F8() { return kernel_ei(); }  // EIntr (diei.o, after DIntr)

// gcc 2.x's builtin that the game's exception runtime (frame registration, init_reg_size_table)
// calls: fills the DWARF register size table. The EE's 32 GPRs and 32 FPRs; unwinding through the
// original MIPS tables is not used natively.
extern "C" void gt4_init_dwarf_reg_size_table(unsigned char *table) __asm__("___builtin_init_dwarf_reg_size_table");
extern "C" void gt4_init_dwarf_reg_size_table(unsigned char *table) {
    for (int i = 0; i < 32; ++i) table[i] = 8;
    for (int i = 32; i < 66; ++i) table[i] = 4;
}

// thread.o's StartThread wrapper (005AEDE0) around the syscall.
extern "C" int gt4_sys_StartThread(int id, uint32_t arg);
extern "C" int gt4_replace_StartThread(int id, uint32_t arg) { return gt4_sys_StartThread(id, arg); }

// Debugging aid: how many x87 registers are in use (8 - the empty ones in the tag word). Game code
// must leave the x87 stack empty between functions; a float result called as void leaks one.
extern "C" int gt4_fpu_depth() {
    unsigned short env[14];
    __asm__ volatile("fnstenv %0; fldenv %0" : "=m"(env));
    int used = 0;
    for (int r = 0; r < 8; ++r) used += ((env[4] >> (2 * r)) & 3) != 3;
    return used;
}
