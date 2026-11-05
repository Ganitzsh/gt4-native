// in_guest_ram (runtime/guest.h): the game runtime's test for reading a range of game memory without asking the
// system (frame.cpp, native/pgl.cpp). A range counts only when it lies whole in the RAM section.
#include <cstdio>
#include <cstring>

// (guest.h is the game runtime's, built by gcc: its one builtin, for this MSVC test)
#define __builtin_memcpy std::memcpy
#include "../runtime/guest.h"

using gt4::rt::in_guest_ram;
using gt4::rt::kGuestSectionStart;
using gt4::rt::kRamSize;

int main() {
    int failures = 0;
    auto expect = [&](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAILED: %s\n", what);
            ++failures;
        }
    };
    expect(in_guest_ram(kGuestSectionStart, 16), "a range at the section's start");
    expect(in_guest_ram(0x00100000, 4096), "a range in the middle");
    expect(in_guest_ram(kRamSize - 16, 16), "a range ending at the section's end");
    expect(in_guest_ram(kRamSize, 0), "nothing at the end");
    expect(!in_guest_ram(kRamSize - 16, 17), "a range starting inside and ending one byte past the end");
    expect(!in_guest_ram(kRamSize - 8, 4096), "a range starting inside and running far past the end");
    expect(!in_guest_ram(kGuestSectionStart - 1, 16), "a range starting below the section (the null page)");
    expect(!in_guest_ram(0, 16), "address 0");
    expect(!in_guest_ram(kRamSize, 1), "a range starting at the end");
    expect(!in_guest_ram(0x10000000, 16), "a range far above the section");
    expect(!in_guest_ram(kGuestSectionStart, static_cast<size_t>(-1)), "a length that would overflow the end");
    expect(!in_guest_ram(static_cast<uintptr_t>(-8), 16), "an address near the top whose end wraps round");
    std::printf("guest_ram: %s\n", failures ? "FAILED" : "ranges checked");
    return failures ? 1 : 0;
}
