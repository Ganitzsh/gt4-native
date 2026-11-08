// Differential tests of double-precision code in the libstream unit (005547E8); see difftest.h. Their outputs are
// doubles, compared whole (Test::double_addr/double_size): the EE computes them with its soft-float routines, the
// native draft with x87 doubles, so the last bits may differ.
#include "difftest.h"

#include <cstdint>
#include <cstring>

using namespace difftest;

namespace {

void putd(std::uint32_t at, double v) { std::memcpy(ram(at), &v, 8); }
double rndd(double lo, double hi) { return lo + (hi - lo) * (rnd() % 1000001) / 1000000.0; }

constexpr std::uint32_t kState = kScratch + 0x1000, kPose = kScratch, kOut = kScratch + 0x2000;

// 005570E0 (state, pose, out): pose holds a translation (+0x00..+0x10) and three angles (+0x18..+0x28) that
// 0059C5C0 / 0059CE48 turn into a rotation. For each of six legs it rotates and moves one point of the state
// (+0x88, +0x90, +0x98 by 0x18 steps), takes its distance to a second point (+0x118, +0x120, +0x128 by 0x18
// steps) through 0059D318, minus 581.9016, into +0x208; smooths that against +0x1A8 and +0x1D8 (a ±50 threshold
// picks the branch) into +0x238; and writes (int)((x + 68) * 22.7) of the six smoothed values to out[0..5].
// The ranges keep the lengths near 581.9 and the previous values on both sides of ±50.
void six_legs(std::uint32_t a[8]) {
    fill(kScratch, 0x3000);
    putd(kPose + 0x00, rndd(-20, 20));
    putd(kPose + 0x08, rndd(-20, 20));
    putd(kPose + 0x10, rndd(-20, 20));
    for (int k = 0; k < 3; ++k) putd(kPose + 0x18 + 8 * k, rndd(-0.3, 0.3));
    for (int leg = 0; leg < 6; ++leg) {
        for (int c = 0; c < 3; ++c) {
            putd(kState + 0x88 + 0x18 * leg + 8 * c, rndd(-250, 250));           // the moved point
            putd(kState + 0x118 + 0x18 * leg + 8 * c, rndd(-250, 250) + (c == 2 ? 450 : 0));   // the fixed one
        }
        putd(kState + 0x1A8 + 8 * leg, rndd(-100, 100));
        putd(kState + 0x1D8 + 8 * leg, rndd(-100, 100));
        putd(kState + 0x208 + 8 * leg, rndd(-100, 100));
        putd(kState + 0x238 + 8 * leg, rndd(-100, 100));
    }
    a[0] = kState;
    a[1] = kPose;
    a[2] = kOut;
}

Test six_legs_test() {
    Test t{"005570E0", 0x005570E0, "iii", 'v', true, six_legs};
    t.double_addr = kState;
    t.double_size = 0x268;
    return t;
}
const Register r1(six_legs_test());

}  // namespace
