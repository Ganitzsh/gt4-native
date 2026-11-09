#include "xinput_direct.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>
#endif

#include <cstdio>

namespace gt4 {

namespace {
bool g_on = false;   // the last xinputRumbleAll asked for the motors (main thread only)
}  // namespace

void xinputRumbleStop() {
    if (g_on) xinputRumbleAll(0, 0);
}

std::string xinputRumbleAll(uint16_t low, uint16_t high) {
    g_on = low || high;
#ifdef _WIN32
    // loaded as SDL loads it (the newest the system has), nothing linked: a build without the library still starts
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using SetState = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    static const HMODULE dll = [] {
        for (const char* name : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"})
            if (const HMODULE h = LoadLibraryA(name)) return h;
        return HMODULE(nullptr);
    }();
    if (!dll) return "no XInput library";
    const auto get = reinterpret_cast<GetState>(reinterpret_cast<void*>(GetProcAddress(dll, "XInputGetState")));
    const auto set = reinterpret_cast<SetState>(reinterpret_cast<void*>(GetProcAddress(dll, "XInputSetState")));
    if (!get || !set) return "no XInputGetState or XInputSetState";
    std::string out;
    for (DWORD slot = 0; slot < 4; ++slot) {
        XINPUT_STATE state{};
        if (get(slot, &state) != ERROR_SUCCESS) continue;
        XINPUT_VIBRATION v{};
        v.wLeftMotorSpeed = low;
        v.wRightMotorSpeed = high;
        const DWORD result = set(slot, &v);
        char text[64];
        if (result == ERROR_SUCCESS)
            std::snprintf(text, sizeof text, "%sslot %lu ok", out.empty() ? "" : ", ",
                          static_cast<unsigned long>(slot));
        else
            std::snprintf(text, sizeof text, "%sslot %lu failed (%lu)", out.empty() ? "" : ", ",
                          static_cast<unsigned long>(slot), static_cast<unsigned long>(result));
        out += text;
    }
    return out.empty() ? "no XInput pad connected" : out;
#else
    (void)low;
    (void)high;
    return "not on this system";
#endif
}

}  // namespace gt4
