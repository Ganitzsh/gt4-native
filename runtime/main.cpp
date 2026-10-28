// gt4_game: the game's own code (compiled from gt4-decomp) running natively on the PS2 memory map.
//
//   gt4_game [--elf PATH] [--iso PATH] [--log PATH]
//
// Boot: map guest memory, load the original executable's segments (the data the game code reads),
// put a trampoline at every original function address, then call the game's main (0x00107F08)
// on a game thread, as crt0 would after _InitSys.
#include "guest.h"
#include "image.h"
#include "kernel.h"
#include "log.h"
#include "runtime.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int kernel_self_test();

namespace gt4::rt {

RuntimeConfig g_config;

namespace {

constexpr uint32_t kGameMain = 0x00107F08;

// The PS2 FPU is single precision: make the x87 unit (which the game code compiled by gcc 2.9 uses)
// round every result to 24 bits. Host code is compiled for SSE and is not affected.
void set_guest_fpu() {
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw = (cw & ~0x0300) | 0x0000;  // precision control: single
    if (const char *pc = getenv("GT4_FPU_PC"))  // testing: double or extended precision instead
        cw |= pc[0] == 'd' ? 0x0200 : pc[0] == 'e' ? 0x0300 : 0;
    if (getenv("GT4_FPU_TRAP"))  // testing: fault on the first invalid operation, divide by zero or overflow
        cw &= ~0x000D;               // (the EE has no inf or NaN: it clamps; IEEE results here diverge)
    __asm__ volatile("fldcw %0" : : "m"(cw));
}

bool g_self_test = false;

DWORD WINAPI game_thread(void *) {
    set_guest_fpu();
    kernel_init();  // this host thread is the guest main thread (id 1)
    kernel_install_syscalls();
    services_init();
    kernel_start_interrupts();
    if (g_self_test) return static_cast<DWORD>(kernel_self_test());
    static char arg0[] = "cdrom0:\\CORE.GT4;1";
    static char *argv[] = {arg0, nullptr};
    // What crt0 does after _InitSys (which the native kernel stands in for): 0048EF90 sets up the
    // game's heap (00575C78) and the time zone (005524B8). The .bss is already zero.
    reinterpret_cast<void (*)()>(guest_ptr(0x0048EF90))();
    log_info("calling the game's main (%08x)", kGameMain);
    auto game_main = reinterpret_cast<int (*)(int, char **)>(guest_ptr(kGameMain));
    const int rc = game_main(1, argv);
    log_info("the game's main returned %d", rc);
    return static_cast<DWORD>(rc);
}

const char *arg_value(int argc, char **argv, const char *name, const char *env, const char *fallback) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    const char *e = getenv(env);
    return e && *e ? e : fallback;
}

}  // namespace
}  // namespace gt4::rt

int main(int argc, char **argv) {
    using namespace gt4::rt;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--self-test") == 0) g_self_test = true;
    g_config.elf_path = arg_value(argc, argv, "--elf", "GT4_ELF", "orig/SCUS-97328/CORE.GT4.elf");
    static std::string default_iso;
    {
        // next to the executable as gt4-decomp keeps it: orig/SCUS-97328/<name>.iso beside CORE.GT4.elf
        std::string elf = g_config.elf_path;
        const size_t slash = elf.find_last_of("/\\");
        default_iso = (slash == std::string::npos ? std::string() : elf.substr(0, slash + 1)) +
                      "Gran Turismo 4 (USA) (v1.01).iso";
    }
    g_config.iso_path = arg_value(argc, argv, "--iso", "GT4_ISO", default_iso.c_str());
    g_config.link_name = arg_value(argc, argv, "--link", "GT4_LINK", "");
    g_config.capture_path = arg_value(argc, argv, "--capture", "GT4_CAPTURE", "");
    static std::string default_card;
    if (const char *la = getenv("LOCALAPPDATA")) default_card = std::string(la) + "\\gt4-native\\memcard1";
    else default_card = "memcard1";
    g_config.memcard_path = arg_value(argc, argv, "--memcard", "GT4_MEMCARD", default_card.c_str());
    g_config.aspect = arg_value(argc, argv, "--aspect", "GT4_ASPECT", "");
    g_config.menus = arg_value(argc, argv, "--menus", "GT4_MENUS", "centered");
    log_open(arg_value(argc, argv, "--log", "GT4_LOG", "build/game/run.log"));
    log_info("gt4_game: %u native functions linked", native_count());
    if (!guest_memory_init()) return 2;
    if (!load_elf(g_config.elf_path)) return 2;
    kernel_scan_syscalls();
    install_trampolines();
    HANDLE t = CreateThread(nullptr, 64u << 20, game_thread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!t) fatal("cannot start the game thread (Windows error %lu)", GetLastError());
    WaitForSingleObject(t, INFINITE);
    DWORD rc = 0;
    GetExitCodeThread(t, &rc);
    return static_cast<int>(rc);
}
