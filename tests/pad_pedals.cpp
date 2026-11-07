// input.trigger_travel = full (runtime/pad_pedals.h): the rescaled pressures, run through GT4's own pressure curve
// (the DualShock 2 apply step 0043BBC0: min(min(2p, 256) * 100 / K, 256) << 4, K 35 accelerate, 65 brake), reach
// full accelerate and brake only at the end of the travel, rise with it, and only on the buttons that carry them.
#include "pad_pedals.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pad = gt4::rt::pad;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

// GT4's value (0..4096) for a pressure, accelerate (K 35) or brake (K 65).
int game_value(unsigned p, unsigned k) { return static_cast<int>(std::min(std::min(2 * p, 256u) * 100 / k, 256u) << 4); }

}  // namespace

int main() {
    expect(pad::rescale(0, 45) == 0 && pad::rescale(255, 45) == 255 && pad::rescale(255, 84) == 255, "the ends");
    // the first travel at which GT4 gives full accelerate / brake: not before 95 % (with game: 18 % and 33 %)
    for (unsigned k : {35u, 65u}) {
        const uint8_t full = k == 35 ? pad::kAccelerateFull : pad::kBrakeFull;
        int first = -1, game_first = -1;
        bool rising = true;
        int last = 0;
        for (unsigned travel = 0; travel <= 255; ++travel) {
            const int v = game_value(pad::rescale(static_cast<uint8_t>(travel), full), k);
            rising = rising && v >= last;
            last = v;
            if (first < 0 && v == 4096) first = static_cast<int>(travel);
            if (game_first < 0 && game_value(travel, k) == 4096) game_first = static_cast<int>(travel);
        }
        std::printf("%s: full from travel %d/255 (%.1f %%) with full, %d/255 (%.1f %%) with game\n",
                    k == 35 ? "accelerate" : "brake", first, 100.0 * first / 255, game_first, 100.0 * game_first / 255);
        expect(first >= 242, "full only from about 95 % of the travel");
        expect(game_first == (k == 35 ? 45 : 84), "game: GT4's own knee");
        expect(rising, "the value rises with the travel");
        expect(game_value(pad::rescale(128, full), k) > 1900 && game_value(pad::rescale(128, full), k) < 2200,
               "half the travel gives about half");
    }
    // the default assignment: Cross (pressure 6) and Square (7) rescaled; R2 (11), shift up, left alone
    uint8_t frame[18] = {};
    std::memset(frame + 6, 128, 12);
    expect(pad::rescale_pedals(frame, pad::kDefaultButtons) == 2, "two pedals by default");
    expect(frame[6 + 6] == pad::rescale(128, 45) && frame[6 + 7] == pad::rescale(128, 84), "Cross and Square");
    expect(frame[6 + 11] == 128 && frame[6 + 10] == 128 && frame[6 + 5] == 128, "R2, L2 and Circle left alone");
    // accelerate moved to R2 and brake to L2 in GT4's Options: those are rescaled instead
    int8_t moved[16];
    std::memcpy(moved, pad::kDefaultButtons, 16);
    moved[6] = 3;   // Cross: shift up
    moved[7] = 2;   // Square: shift down
    moved[11] = pad::kAccelerate;
    moved[10] = pad::kBrake;
    std::memset(frame + 6, 128, 12);
    pad::rescale_pedals(frame, moved);
    expect(frame[6 + 11] == pad::rescale(128, 45) && frame[6 + 10] == pad::rescale(128, 84) && frame[6 + 6] == 128 &&
               frame[6 + 7] == 128,
           "the pedals follow GT4's assignment");
    // the bits are left alone
    frame[0] = 0x12;
    frame[1] = 0x34;
    pad::rescale_pedals(frame, moved);
    expect(frame[0] == 0x12 && frame[1] == 0x34, "the pressed bits are left alone");
    if (failures) return 1;
    std::printf("pad pedals: all checks passed\n");
    return 0;
}
