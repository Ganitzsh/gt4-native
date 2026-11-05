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

}  // namespace gt4
