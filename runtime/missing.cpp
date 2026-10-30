// Calls to functions of the original image that have no native code yet.
//
// GT4_MISSING=stop (default): log the function, its caller and stop the process, exit code 4. This is
// the G6 rule: a function that is still missing stops the game with a log of its address.
// GT4_MISSING=continue: log the first call of each missing function and return 0, to see how far the
// game gets (a development aid; results after the first missing call are not trustworthy).
#include "image.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>

namespace gt4::rt {
namespace {

std::mutex g_mutex;
std::set<uint32_t> g_seen;

bool continue_mode() {
    static const bool v = [] {
        const char *s = getenv("GT4_MISSING");
        return s && std::strcmp(s, "continue") == 0;
    }();
    return v;
}

}  // namespace
}  // namespace gt4::rt

extern "C" void gt4_missing(uint32_t fn, uint32_t caller) {
    using namespace gt4::rt;
    uint32_t caller_fn = 0;
    const char *caller_name = native_name_for(caller, &caller_fn);
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool first = g_seen.insert(fn).second;
    if (!continue_mode()) {
        log_error("MISSING function %08x called from %s (%08x), host return address %08x; stopping", fn,
                  *caller_name ? caller_name : "?", caller_fn, caller);
        fflush(stderr);
        ExitProcess(4);
    }
    if (first)
        log_warn("missing function %08x called from %s (%08x); returning 0", fn, *caller_name ? caller_name : "?",
                 caller_fn);
}

// A game function the native program must never run: the game_src/ stub of a hardware path whose whole user the
// runtime replaced (the IPU behind the movie loop 0054E3D8, runtime/native/movie.cpp). Reports the function, why,
// and its caller as gt4_missing does, and stops whatever GT4_MISSING says: going on would mean the hardware it drives
// silently did nothing.
extern "C" void gt4_not_native(uint32_t fn, const void *caller, const char *why) {
    uint32_t caller_fn = 0;
    const char *caller_name = gt4::rt::native_name_for(reinterpret_cast<uintptr_t>(caller), &caller_fn);
    gt4::rt::fatal("NOT NATIVE function %08x called from %s (%08x), host return address %08x: %s", fn,
                   *caller_name ? caller_name : "?", caller_fn,
                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(caller)), why ? why : "");
}

// A symbol the game code uses that has neither a native definition nor a known PS2 address
// (tools/game_link.py makes a stub for each).
extern "C" void gt4_unresolved(const char *name) {
    gt4::rt::log_error("UNRESOLVED symbol %s called; stopping", name);
    fflush(stderr);
    ExitProcess(5);
}
