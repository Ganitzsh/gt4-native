// platform/input/wheel_ffb.cpp (PLAN.md D7): GT4's three LGDEV effect blocks to the forces the host plays, on the
// values R15's race recorded (spikes/R15-lgdev-effects.md "Live"; numbers only): effect A, a constant force, at
// magnitudes -205, 55 and 255 (the race's lowest, median and highest); B, a damper, set to 32, 32, 128, 255; C, a
// spring, set to 128 on all four. Checked: the strength scales every level and coefficient; the 60 % ceiling holds
// whatever the strength; the sign follows the magnitude and the configured direction; a stopped effect is off but
// keeps its parameters; an unknown type, a second effect of one kind and a count past three are ignored; the
// spring is played centred whatever GT4's offset (its scale unknown), the offset reported.
#include "wheel_ffb.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace gt4::input;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

void put(uint8_t* block, int offset, int32_t v) { std::memcpy(block + offset, &v, 4); }

Gt4WheelEffect constant(int32_t magnitude, uint32_t state = GT4_WHEEL_FX_RUNNING) {
    Gt4WheelEffect e{};
    e.state = state;
    put(e.block, 0x00, 0);
    put(e.block, 0x04, -1);
    put(e.block, 0x0C, magnitude);
    put(e.block, 0x10, 90);
    return e;
}

Gt4WheelEffect conditionEffect(uint8_t type, int32_t coeffR, int32_t coeffL, int32_t satR, int32_t satL,
                               uint32_t state = GT4_WHEEL_FX_RUNNING, int32_t centre = 0) {
    Gt4WheelEffect e{};
    e.state = state;
    put(e.block, 0x00, type);
    put(e.block, 0x04, -1);
    put(e.block, 0x0C, centre);
    put(e.block, 0x14, coeffR);
    put(e.block, 0x18, coeffL);
    put(e.block, 0x1C, satR);
    put(e.block, 0x20, satL);
    return e;
}

// The race's set: A, B (damper), C (spring), in GT4's download order.
Gt4WheelFx race(int32_t magnitude, uint32_t stateA = GT4_WHEEL_FX_RUNNING) {
    Gt4WheelFx fx{};
    fx.index = 0;
    fx.count = 3;
    fx.effects[0] = constant(magnitude, stateA);
    fx.effects[1] = conditionEffect(0x10, 32, 32, 128, 255);
    fx.effects[2] = conditionEffect(0x07, 128, 128, 128, 128);
    return fx;
}

bool near(long a, long b, long tolerance = 1) { return std::labs(a - b) <= tolerance; }

}  // namespace

int main() {
    // full strength: the median magnitude unscaled, the top one held at the ceiling
    {
        const WheelForces f = wheelForces(race(55), {1.0f, 1});
        expect(f.constantOn && f.springOn && f.damperOn, "the race's three effects on");
        expect(near(f.constantLevel, std::lround(55.0 / 255.0 * 32767.0)), "A at 55: 55 / 255 of full scale");
        const WheelForces top = wheelForces(race(255), {1.0f, 1});
        expect(near(top.constantLevel, std::lround(0.60 * 32767.0)), "A at 255: held at the 60 % ceiling");
        const WheelForces low = wheelForces(race(-205), {1.0f, 1});
        expect(near(low.constantLevel, -std::lround(0.60 * 32767.0)), "A at -205 (80 %): held at the ceiling, negative");
        // the damper: coefficients 32, saturations 128 and 255 (the last at the ceiling)
        expect(near(f.damper.rightCoeff, std::lround(32.0 / 255.0 * 32767.0)) &&
                   near(f.damper.leftCoeff, std::lround(32.0 / 255.0 * 32767.0)), "B's coefficients 32");
        expect(near(f.damper.rightSat, std::lround(128.0 / 255.0 * 65535.0)), "B's positive saturation 128");
        expect(near(f.damper.leftSat, std::lround(0.60 * 65535.0)), "B's negative saturation 255: at the ceiling");
        expect(f.damper.center == 0, "a damper has no centre");
        // the spring: 128 on all four
        expect(near(f.spring.rightCoeff, std::lround(128.0 / 255.0 * 32767.0)) &&
                   near(f.spring.leftCoeff, f.spring.rightCoeff) &&
                   near(f.spring.rightSat, std::lround(128.0 / 255.0 * 65535.0)) &&
                   near(f.spring.leftSat, f.spring.rightSat), "C at 128");
        expect(f.spring.center == 0, "C centred");
    }
    // the first live run's 15 %: everything scaled, nothing near the ceiling
    {
        const WheelForces f = wheelForces(race(-205), {0.15f, 1});
        expect(near(f.constantLevel, -std::lround(205.0 / 255.0 * 0.15 * 32767.0)), "A at -205, 15 %");
        expect(near(f.damper.leftSat, std::lround(0.15 * 65535.0)), "B's 255 saturation at 15 %");
        expect(near(f.spring.rightCoeff, std::lround(128.0 / 255.0 * 0.15 * 32767.0)), "C at 15 %");
    }
    // the strength's range: 0 is silence, past 1 is still 1 (the ceiling holds)
    {
        const WheelForces zero = wheelForces(race(255), {0.0f, 1});
        expect(zero.constantLevel == 0 && zero.damper.rightCoeff == 0 && zero.spring.leftSat == 0, "strength 0: none");
        const WheelForces over = wheelForces(race(255), {5.0f, 1});
        expect(near(over.constantLevel, std::lround(0.60 * 32767.0)), "strength past 1: the ceiling holds");
        expect(over.spring.rightSat <= std::lround(0.60 * 65535.0) + 1, "strength past 1: the spring's ceiling");
    }
    // the sign: the configured direction flips A, never the conditions
    {
        const WheelForces f = wheelForces(race(55), {1.0f, -1});
        expect(near(f.constantLevel, -std::lround(55.0 / 255.0 * 32767.0)), "constantSign -1 flips A");
        expect(f.damper == wheelForces(race(55), {1.0f, 1}).damper, "constantSign leaves B alone");
    }
    // states: a stopped effect is off, its parameters kept for the start; none is skipped
    {
        const WheelForces f = wheelForces(race(55, GT4_WHEEL_FX_STOPPED), {1.0f, 1});
        expect(!f.constantOn && f.constantLevel != 0, "A stopped: off, its level kept");
        Gt4WheelFx fx = race(55);
        fx.effects[1].state = GT4_WHEEL_FX_NONE;
        const WheelForces g = wheelForces(fx, {1.0f, 1});
        expect(!g.damperOn && g.damper == WheelCondition{}, "B not downloaded: no damper");
    }
    // the edges: an unknown type, a second constant, a count past three, magnitudes past 255
    {
        Gt4WheelFx fx = race(55);
        fx.effects[1] = constant(-255);   // a second constant force replaces the damper: ignored
        fx.effects[2].block[0] = 0x33;    // an unknown type: ignored
        const WheelForces f = wheelForces(fx, {1.0f, 1});
        expect(f.constantOn && near(f.constantLevel, std::lround(55.0 / 255.0 * 32767.0)), "the first constant wins");
        expect(!f.springOn && !f.damperOn, "an unknown type and a duplicate: nothing else on");
        fx.count = 1000;
        expect(wheelForces(fx, {1.0f, 1}) == f, "a count past three: three read");
        expect(wheelForces(race(100000), {0.15f, 1}).constantLevel == wheelForces(race(255), {0.15f, 1}).constantLevel,
               "a magnitude past 255 is 255");
        Gt4WheelFx centred = race(55);
        centred.effects[2] = conditionEffect(0x07, 128, 128, 128, 128, GT4_WHEEL_FX_RUNNING, -70000);
        const WheelForces c = wheelForces(centred, {1.0f, 1});
        expect(c.spring.center == 0 && c.springCentreRaw == -70000,
               "the spring's centre played at 0, GT4's offset reported for the log");
    }
    expect(wheelForcesOff() == WheelForces{}, "the stops: all off");
    expect(wheelEffectKind(0) == WheelEffectKind::Constant && wheelEffectKind(7) == WheelEffectKind::Spring &&
               wheelEffectKind(0x10) == WheelEffectKind::Damper && wheelEffectKind(1) == WheelEffectKind::None,
           "GT4's three types");
    if (failures) return 1;
    std::printf("input_wheel_ffb: all checks passed\n");
    return 0;
}
