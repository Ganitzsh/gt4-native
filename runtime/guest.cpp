#include "guest.h"
#include "log.h"

#include <windows.h>

#include <cstdlib>

extern "C" unsigned char gt4_guest_ram[];
extern "C" uint32_t gt4_pgl_frame_buffers();   // native/pgl.cpp

namespace gt4::rt {
namespace {

// A second and third view of the executable image: its .guest section is shared, so the views'
// .guest pages are the same physical pages as the original's (FILE_MAP_COPY keeps shared sections
// shared and writable). The rest of each view is never used.
// Wine (Proton) places a further view of an image by the image's base, whatever address is asked for, but keeps it
// in the address range asked for (NtMapViewOfSectionEx's address requirements; kernelbase's MapViewOfFile3 would
// take the success status STATUS_IMAGE_NOT_AT_BASE for a failure).
void *map_image_in_range(HANDLE section, void *want) {
    struct ExtendedParameter { DWORD64 type; DWORD64 pointer; };          // MEM_EXTENDED_PARAMETER
    struct AddressRequirements { void *lowest; void *highest; SIZE_T alignment; };
    using MapEx = LONG(WINAPI *)(HANDLE, HANDLE, void **, LARGE_INTEGER *, SIZE_T *, ULONG, ULONG, ExtendedParameter *, ULONG);
    const auto map = reinterpret_cast<MapEx>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtMapViewOfSectionEx"));
    if (!map) return nullptr;
    const auto *image = reinterpret_cast<const char *>(GetModuleHandleW(nullptr));
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(image + reinterpret_cast<const IMAGE_DOS_HEADER *>(image)->e_lfanew);
    AddressRequirements range{want, static_cast<char *>(want) + nt->OptionalHeader.SizeOfImage - 1, 0};
    ExtendedParameter parameter{1, reinterpret_cast<UINT_PTR>(&range)};   // MemExtendedParameterAddressRequirements
    void *view = nullptr;
    SIZE_T size = 0;
    if (map(section, GetCurrentProcess(), &view, nullptr, &size, 0, PAGE_EXECUTE_WRITECOPY, &parameter, 1) < 0) return nullptr;
    return view;
}

bool map_mirror(HANDLE section, uint32_t mirror_base) {
    void *want = guest_ptr(mirror_base + 0x00010000);  // the image base, shifted
    void *view = MapViewOfFileEx(section, FILE_MAP_COPY | FILE_MAP_EXECUTE, 0, 0, 0, want);
    if (view && view != want) {
        UnmapViewOfFile(view);
        view = map_image_in_range(section, want);
    }
    if (view != want) {
        log_error("cannot map the RAM mirror at %08x (Windows error %lu)", mirror_base, GetLastError());
        return false;
    }
    DWORD old;
    VirtualProtect(guest_ptr(mirror_base + 0x00010000), 0x1000, PAGE_NOACCESS, &old);  // headers
    return true;
}

bool alloc_fixed(uint32_t base, uint32_t size, const char *name, bool commit = true) {
    void *p = VirtualAlloc(guest_ptr(base), size, commit ? MEM_RESERVE | MEM_COMMIT : MEM_RESERVE, commit ? PAGE_READWRITE : PAGE_NOACCESS);
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
    // No game code reaches the PS2's hardware natively: its blocks are reserved without memory, so an access left
    // over faults and the crash report names its function. (Reserved, not left free: memory Windows placed there,
    // a thread's stack, would be misread, as the graphics library keeps addresses in 28 bits.) GT4_HW_AUDIT gives
    // them memory without access, to list such accesses and go on (hw_audit.cpp).
    const char *audit = getenv("GT4_HW_AUDIT");
    for (const HwBlock &b : kHwBlocks)
        if (!alloc_fixed(b.base, b.size, b.name, audit && *audit && *audit != '0')) return false;
    if (!alloc_fixed(kScratchpad, kScratchpadSize, "scratchpad")) return false;
    gt4_pgl_frame_buffers();
    log_info("guest memory: RAM %08x-%08x, mirrors at %08x and %08x, scratchpad %08x", kGuestSectionStart,
             kRamSize, kUncachedMirror, kUncachedAccelMirror, kScratchpad);
    return true;
}

}  // namespace gt4::rt
