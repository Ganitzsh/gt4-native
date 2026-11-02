// 002F5EC0: makes a script exception (an hException holding the message) and its handle, for a builtin or the VM to
// throw. Retail GT4 prints no script error; the port logs each message (the first 200), so a script that stops
// halfway (a try/catch swallowing the error) shows why.
#include "guest.h"
#include "log.h"

#include <cstdint>

using namespace gt4::rt;

namespace {
template <class F> F guest_fn(uint32_t addr) { return reinterpret_cast<F>(guest_ptr(addr)); }
}  // namespace

extern "C" void gt4_replace_func_002F5EC0(uint32_t handle, uint32_t message) {
    static int logged = 0;
    if (logged < 200) {
        ++logged;
        const uint32_t text = guest_ref<uint32_t>(message);   // String: its characters, the length 16 bytes before
        const int len = text ? guest_ref<int32_t>(text - 16) : 0;
        log_warn("script exception: %.*s", len > 0 && len < 400 ? len : 0, static_cast<const char *>(guest_ptr(text)));
    }
    const uint32_t object = guest_fn<uint32_t (*)(int, int, const char *)>(0x00326750)(0x14, 4, "RefCounter");
    guest_fn<void (*)(uint32_t, uint32_t)>(0x002F5CC8)(object, message);
    uint32_t local = object;
    guest_fn<void (*)(uint32_t, uint32_t *)>(0x00309348)(handle, &local);
}
