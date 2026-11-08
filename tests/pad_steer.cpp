// runtime/pad_steer.h (D12): GT4's steering band compensated in the left stick's byte. 0 permille changes nothing;
// the centre stays the centre; at 29 % the first byte off the centre lands on the band's edge (165 right, 91 left),
// every byte past it is reached, one at most a step, never back, and full stick stays 255 and 0.
#include "pad_steer.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using gt4::rt::pad::steer_band;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    bool identity = true;
    for (int b = 0; b < 256; ++b) identity &= steer_band(static_cast<uint8_t>(b), 0) == b;
    expect(identity, "0 permille: every byte as it is");
    expect(steer_band(128, 290) == 128, "the centre stays the centre");
    expect(steer_band(129, 290) == 165 && steer_band(127, 290) == 91, "the first byte off the centre: the band's edge");
    expect(steer_band(255, 290) == 255 && steer_band(0, 290) == 0, "full stick: 255 and 0");
    for (int side : {1, -1}) {
        int last = steer_band(static_cast<uint8_t>(128 + side), 290);
        bool step = true, back = false, every = true;
        bool seen[256] = {};
        for (int d = 1; d <= (side > 0 ? 127 : 128); ++d) {
            const int out = steer_band(static_cast<uint8_t>(128 + side * d), 290);
            step &= std::abs(out - last) <= 1;
            back |= side > 0 ? out < last : out > last;
            seen[out] = true;
            last = out;
        }
        for (int b = side > 0 ? 165 : 91; b != (side > 0 ? 256 : -1); b += side) every &= seen[b];
        expect(step && !back && every, side > 0 ? "right: every byte from 165 to 255, one a step at most, never back"
                                                : "left: every byte from 91 to 0, one a step at most, never back");
    }
    expect(steer_band(200, 5000) == steer_band(200, 1000), "permille over 1000: clamped");
    if (failures) return 1;
    std::printf("pad_steer: all checks passed\n");
    return 0;
}
