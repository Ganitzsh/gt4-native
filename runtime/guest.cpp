#include "guest.h"
#include "log.h"

#include <windows.h>

extern "C" unsigned char gt4_guest_ram[];

namespace gt4::rt {
namespace {

// A second and third view of the executable image: its .guest section is shared, so the views'
// .guest pages are the same physical pages as the original's (FILE_MAP_COPY keeps shared sections
// shared and writable). The rest of each view is never used.
bool map_mirror(HANDLE section, uint32_t mirror_base) {
    void *want = guest_ptr(mirror_base + 0x00010000);  // the image base, shifted
    void *view = MapViewOfFileEx(section, FILE_MAP_COPY | FILE_MAP_EXECUTE, 0, 0, 0, want);
    if (view != want) {
        log_error("cannot map the RAM mirror at %08x (Windows error %lu)", mirror_base, GetLastError());
        return false;
    }
    DWORD old;
    VirtualProtect(guest_ptr(mirror_base + 0x00010000), 0x1000, PAGE_NOACCESS, &old);  // headers
    return true;
}

bool alloc_fixed(uint32_t base, uint32_t size, const char *name) {
    void *p = VirtualAlloc(guest_ptr(base), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p != guest_ptr(base)) {
        log_error("cannot map %s at %08x (Windows error %lu)", name, base, GetLastError());
        return false;
    }
    return true;
}

}  // namespace

bool guest_memory_init() {
    if (guest_addr(gt4_guest_ram) != kGuestSectionStart) {
        log_error("the guest RAM section is at %p, not %08x: link with tools/game_link.py", gt4_guest_ram,
                  kGuestSectionStart);
        return false;
    }
    // The shared section would also be shared with a second running copy of this executable.
    CreateMutexW(nullptr, TRUE, L"Local\\gt4_game_guest_ram");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log_error("another gt4_game is running; its guest memory would be shared with this one");
        return false;
    }
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    HANDLE section =
        file == INVALID_HANDLE_VALUE ? nullptr
                                     : CreateFileMappingW(file, nullptr, PAGE_EXECUTE_WRITECOPY | SEC_IMAGE, 0, 0, nullptr);
    if (!section) {
        log_error("cannot open the executable image for the RAM mirrors (Windows error %lu)", GetLastError());
        return false;
    }
    if (!map_mirror(section, kUncachedMirror) || !map_mirror(section, kUncachedAccelMirror)) return false;
    for (const HwBlock &b : kHwBlocks)
        if (!alloc_fixed(b.base, b.size, b.name)) return false;
    if (!alloc_fixed(kScratchpad, kScratchpadSize, "scratchpad")) return false;
    log_info("guest memory: RAM %08x-%08x, mirrors at %08x and %08x, scratchpad %08x", kGuestSectionStart,
             kRamSize, kUncachedMirror, kUncachedAccelMirror, kScratchpad);
    return true;
}

}  // namespace gt4::rt
