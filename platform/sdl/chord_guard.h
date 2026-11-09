// Select + Start opens the settings menu on a pad (main.cpp). Whichever of the two goes down first reaches the game
// only once no chord can follow: released within kChordMs, it is sent as a short press after its release; held alone
// longer, it is sent from then on. A chord sends neither until both are up. (A Start that reached the game before the
// chord completed paused the race under the menu.)
#pragma once

#include "ps2_pad.h"

#include <cstdint>

namespace gt4 {

class ChordGuard {
public:
    static constexpr uint64_t kChordMs = 200;
    static constexpr int kTapFrames = 4;   // a short press: this many polls, from its release

    // Filters one poll of the pad (the time in milliseconds).
    void filter(ps2::PadInput& input, uint64_t now) {
        using ps2::PadSelect;
        using ps2::PadStart;
        const bool start = input.button[PadStart] > 0.0f, select = input.button[PadSelect] > 0.0f;
        if (start && select) chord = true;
        if (chord) {
            if (!start && !select) chord = false;
            alone = -1;
            input.button[PadStart] = input.button[PadSelect] = 0.0f;
        } else if (start || select) {
            const int which = start ? PadStart : PadSelect;
            if (alone != which) alone = which, since = now;
            if (now - since < kChordMs) input.button[which] = 0.0f;   // a chord may still follow
        } else {
            if (alone >= 0 && now - since < kChordMs) tap = alone, tapFrames = kTapFrames;   // a short press
            alone = -1;
        }
        if (tapFrames > 0) {
            input.button[tap] = 1.0f;
            --tapFrames;
        }
    }

private:
    uint64_t since = 0;    // when `alone` went down
    int alone = -1;        // the one button down alone (PadStart or PadSelect), or -1
    bool chord = false;    // both went down: neither reaches the game until both are up
    int tap = 0, tapFrames = 0;
};

// L3 + R3 held together for kHoldMs: the dump key (main.cpp; PLAN.md C6b, the night bands). L3 or R3 alone reaches
// the game at once, unchanged (GT4 may use either); from the poll both are down until both are up again, neither does,
// so letting one go first sends no stray press. True on the poll the hold completes, once a hold.
class DumpChord {
public:
    static constexpr uint64_t kHoldMs = 250;

    bool filter(ps2::PadInput& input, uint64_t now) {
        using ps2::PadL3;
        using ps2::PadR3;
        const bool l3 = input.button[PadL3] > 0.0f, r3 = input.button[PadR3] > 0.0f;
        if (l3 && r3 && !chord) chord = true, fired = false;
        if (!chord) return false;
        if (!l3 && !r3) {
            chord = false;
            both = false;
            return false;
        }
        input.button[PadL3] = input.button[PadR3] = 0.0f;
        if (l3 && r3 && !both) since = now;   // (both down again after one was up: the hold counts from here)
        both = l3 && r3;
        if (!fired && both && now - since >= kHoldMs) {
            fired = true;
            return true;
        }
        return false;
    }

private:
    uint64_t since = 0;    // when both were last down together, after one was up
    bool chord = false;    // both went down: neither reaches the game until both are up
    bool both = false;     // both down at the last poll
    bool fired = false;    // this hold has dumped
};

}  // namespace gt4
