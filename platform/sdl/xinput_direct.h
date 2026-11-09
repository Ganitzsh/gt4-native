#pragma once
// The rumble test's direct pulse (the Odin's report, 2026-11-10): Steam games rumble on the Odin under Proton while
// our pad, opened by SDL through XInput too, does not. This pulse sets the motors through XInputSetState itself, not
// through SDL, so one run of the test tells SDL's call from the system's.
#include <cstdint>
#include <string>

namespace gt4 {

#ifdef _WIN32
constexpr bool kXInputDirect = true;
#else
constexpr bool kXInputDirect = false;
#endif

// Windows: both motors of every connected XInput pad (slots 0-3 that XInputGetState answers) set through
// XInputSetState, the result per slot ("slot 0 ok"), or why nothing was set; elsewhere "not on this system".
// XInputSetState has no duration (SDL's rumble has): the motors run until something sets them again.
std::string xinputRumbleAll(uint16_t low, uint16_t high);

// The motors set off again if the last xinputRumbleAll turned them on (lane C: a host quit between a direct pulse
// and its stop would leave the pad buzzing at full strength until another program wrote it); else nothing. For
// every way out but a crash: PadDevices' end, --rumble-test's, and a test left with no pad.
void xinputRumbleStop();

}  // namespace gt4
