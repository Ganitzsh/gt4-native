#pragma once
// The original executable in guest memory, and the trampolines that make its function addresses
// callable from native code.
#include <cstdint>

namespace gt4::rt {

// Copies the PT_LOAD segments of the user's CORE.GT4.elf (made by gt4-decomp's tools/core2elf.py) to
// their PS2 addresses. The code segment's bytes are kept: some of it is data (tables in .text).
bool load_elf(const char *path);

// A function of the original image (tools/game_link.py generates both tables).
struct GuestFunction {
    uint32_t addr;
    uint32_t size;  // bytes up to the next function: room for a trampoline
};
struct NativeFunction {
    uint32_t addr;    // the original address
    void *fn;         // the native implementation (compiled decomp source or runtime replacement)
    const char *name;
};

// At every original function address: `jmp native` when a native implementation exists, otherwise
// `call gt4_missing_entry`, which reports the address and stops (or returns 0, see missing.cpp).
void install_trampolines();

// The native function containing a host code address, for reports ("" if none).
const char *native_name_for(uintptr_t host_pc, uint32_t *guest_addr_out);
uint32_t native_count();

// Logs the PS2 addresses of the native game functions on the calling thread's stack (frame chain).
void log_backtrace(const char *tag, int max_frames = 16);

}  // namespace gt4::rt

extern "C" {
extern const gt4::rt::GuestFunction gt4_guest_functions[];
extern const uint32_t gt4_guest_function_count;
extern const gt4::rt::NativeFunction gt4_native_functions[];
extern const uint32_t gt4_native_function_count;
}
