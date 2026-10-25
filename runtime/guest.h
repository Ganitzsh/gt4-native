#pragma once
// The guest address space of the native game process.
//
// The game's own code is compiled for 32-bit x86 and runs on the PS2's memory map unchanged: a guest
// address is a host address. The executable's first section (.guest, runtime/guest.s) is the PS2's
// 32 MB of main RAM, so Windows places nothing else there; the uncached mirrors at 0x20000000 and
// 0x30000000 are second and third views of the same pages; the EE register blocks, VU memory, the GS
// privileged registers and the scratchpad are plain memory at their PS2 addresses, which the native
// replacements of the hardware-facing functions read and write.
#include <cstddef>
#include <cstdint>

namespace gt4::rt {

constexpr uint32_t kRamBase = 0x00000000;
constexpr uint32_t kRamSize = 0x02000000;      // 32 MB
constexpr uint32_t kGuestSectionStart = 0x00011000;  // the first 68 KB stay unmapped (null page)
constexpr uint32_t kUncachedMirror = 0x20000000;
constexpr uint32_t kUncachedAccelMirror = 0x30000000;
constexpr uint32_t kScratchpad = 0x70000000;
constexpr uint32_t kScratchpadSize = 0x4000;

struct HwBlock {
    uint32_t base, size;
    const char *name;
};
// EE registers (timers, IPU, GIF, VIF, DMA, INTC...), VU0/VU1 micro and data memory, GS privileged.
inline constexpr HwBlock kHwBlocks[] = {
    {0x10000000, 0x00010000, "EE registers"},
    {0x11000000, 0x00010000, "VU0/VU1 memory"},
    {0x12000000, 0x00002000, "GS privileged registers"},
};

// Maps the mirrors, the hardware blocks and the scratchpad; checks that the guest RAM section is where
// the link put it. Returns false (with a message on stderr) if any range cannot be mapped.
bool guest_memory_init();

inline void *guest_ptr(uint32_t addr) { return reinterpret_cast<void *>(static_cast<uintptr_t>(addr)); }
inline uint32_t guest_addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <class T> inline T &guest_ref(uint32_t addr) { return *reinterpret_cast<T *>(guest_ptr(addr)); }

}  // namespace gt4::rt
