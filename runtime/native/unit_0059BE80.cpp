// Native code for the hand-written assembly of decomp unit 0059BE80 (Sony's libkernel/libsif).
// Written from the original instructions (gt4-decomp: tools/match.py asm ADDR).
#include "guest.h"

#include <cstdint>

extern "C" {

// 005B0F78 (172 bytes) sceSifWriteBackDCache(addr, size): writes the data cache lines covering
// [addr, addr + size) back to memory (cache 0x18) for the IOP's DMA. The native program has no
// separate cache to write back: nothing to do.
void gt4_replace_func_005B0F78(void *addr, int size) {
    (void)addr;
    (void)size;
}

// 005B7B18 (112 bytes): stores `value` at `address` in kernel mode (interrupts held, Status.ERL
// set, eret back): the timer registers through the uncached kernel window (0xB0001000...). The
// native program has the EE registers at their physical addresses: the kseg0/kseg1 window maps to
// address & 0x1FFFFFFF.
void gt4_replace_func_005B7B18(uint32_t address, uint32_t value) {
    if (address >= 0x80000000u && address < 0xC0000000u) address &= 0x1FFFFFFFu;
    *static_cast<volatile uint32_t *>(gt4::rt::guest_ptr(address & ~3u)) = value;
}

}  // extern "C"
