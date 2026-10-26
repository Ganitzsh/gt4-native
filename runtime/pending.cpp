// Native implementations of game functions that have no matched source yet (or whose source
// cannot compile natively), written from the original's assembly so the boot can go on. Each one
// is temporary: when gt4-decomp matches the function, delete it here and the matched source is
// used instead. The list is also in docs/PENDING_FUNCTIONS.md for the decomp's swarm.
#include "guest.h"

#include <cstdint>
#include <cstring>

using namespace gt4::rt;

extern "C" {

// 0057FBEC (unit 0057A128, 284 bytes): copies the two 64-byte name records D_00655DF0 and
// D_00655E30 to its arguments, returns 0. (Matching draft is 14 instructions off: a v0/v1 swap in
// the second copy under ee-gcc2.96-nosched1.)
int gt4_replace_func_0057FBEC(void *first, void *second) {
    std::memcpy(first, guest_ptr(0x00655DF0), 0x40);
    std::memcpy(second, guest_ptr(0x00655E30), 0x40);
    return 0;
}

// 0039A480 (unit 00394D48, 120 bytes): a constructor (DirectivityMicrophone's member): clears
// +0/+4, vtable 0067E260 at +0x1C, floats +8..+0x18 = 0, then func_0039A530(this, 0, pi/2, 0, 0, 0)
// and func_0039A550(this, 1.0). Drafts are 20 of 30 off.
int gt4_replace_func_0039A480(uint8_t *self) {
    auto set32 = [&](int off, uint32_t v) { std::memcpy(self + off, &v, 4); };
    auto setf = [&](int off, float v) { std::memcpy(self + off, &v, 4); };
    set32(0x4, 0);
    set32(0x1C, 0x0067E260);
    set32(0x0, 0);
    for (int off = 0x8; off <= 0x18; off += 4) setf(off, 0.0f);
    using Set = void (*)(uint8_t *, int32_t, float, float, float, float);
    using Gain = uint32_t (*)(uint8_t *, float);  // a float result, in eax (game code)
    reinterpret_cast<Set>(guest_ptr(0x0039A530))(self, 0, 1.5707964f, 0.0f, 0.0f, 0.0f);
    reinterpret_cast<Gain>(guest_ptr(0x0039A550))(self, 1.0f);
    return 0;
}

// 004B01F8 (56 bytes with 004B0210, unmatched): a file-device thread's exit: longjmp to the jmp_buf
// at +0x50 of its object (set by setjmp in 004B0120) with 1.
void gt4_replace_longjmp(void *buf, int value);
void gt4_replace_func_004B01F8(uint8_t *self) { gt4_replace_longjmp(self + 0x50, 1); }

// 004B0210 (not in the decomp's inventory: glued to 004B01F8): flags +0x3290 and calls 004B0610.
void gt4_replace_func_004B0210(uint8_t *self) {
    const uint32_t one = 1;
    std::memcpy(self + 0x3290, &one, 4);
    reinterpret_cast<void (*)(uint8_t *)>(guest_ptr(0x004B0610))(self);
}

}  // extern "C"
