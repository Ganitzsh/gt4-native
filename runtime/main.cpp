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
    g_config.iso_path = arg_value(argc, argv, "--iso", "GT4_ISO", "");
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
