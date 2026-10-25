// Native code for the hand-written assembly of decomp unit 00100000 (the program start-up).
// Written from the original instructions (gt4-decomp: tools/match.py asm ADDR).
#include "log.h"

extern "C" {

// 00100008 (536 bytes): crt0's entry point (the ELF entry): clears the GPRs, sets $gp and $sp,
// clears .bss, SetupThread / SetupHeap / InitMainThread kernel calls, then main. Nothing in the
// image calls it or holds its address (only the ELF header's e_entry); the native runtime does the
// start-up itself and calls the game's own initialisation, so this is never entered.
[[noreturn]] void gt4_replace_func_00100008(void) {
    gt4::rt::fatal("00100008: crt0's entry point entered natively (the runtime replaces the boot)");
}

}  // extern "C"
