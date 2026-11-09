// One press of a Settings key can reach the host as two requests in the same moment: the Odin's playtest log
// (2026-11-10) showed two "open asked of the game" per press with one pad on port 1, so from two sources of one
// press (a pad button Steam Input also binds to Escape, or Guide with Select + Start). Two requests would open and
// close the host's hub in one event pump, or send the game an open and a close. A request within kWindowMs of the
// last one taken is dropped (main.cpp logs it with its source); a second press a player means is slower than that.
#pragma once

#include <cstdint>

namespace gt4 {

class SettingsRequestGuard {
public:
    static constexpr uint64_t kWindowMs = 150;

    // true: act on this request (the time in milliseconds); false: drop it.
    bool take(uint64_t now) {
        if (taken && now - last < kWindowMs) return false;
        taken = true;
        last = now;
        return true;
    }

private:
    bool taken = false;
    uint64_t last = 0;
};

}  // namespace gt4
