// platform/input/haptics.cpp (PLAN.md D14): the mapping from a car's Vehicle record to the motors and triggers, on a
// synthetic lap built frame by frame in GT4's conventions (slip ratio + braking, - driving): a straight (quiet), a
// corner with understeer (the low motor), a kerb (the high motor, from stroke speed), a lock-up (left trigger, the
// brake's resistance), a spin (right trigger), and a stop (nothing). Each output follows its record in the same frame
// (one-frame steps, no filtering); strength scales everything; triggers off leaves them 0; a record without stroke
// speed gives no kerb.
#include "haptics.h"

#include <cmath>
#include <cstdio>

using namespace gt4;
namespace scene = gt4::gfx::scene;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

// A car going straight at speed, all four wheels down, a little slip of a cruising car.
scene::Vehicle cruising(float speed = 30.0f) {
    scene::Vehicle v{};
    v.prefix = {scene::kVehicle, scene::kVersion};
    v.valid = scene::kVehTransform | scene::kVehVelocity | scene::kVehWheelSlip | scene::kVehWheelForces |
              scene::kVehWheelContact | scene::kVehWheelStroke;
    v.speed = speed;
    for (scene::Wheel& w : v.wheel) {
        w.load = 3000.0f;
        w.onGround = 1;
        w.slipRatio = -0.01f;
        w.slipAngle = 0.005f;
        w.strokeSpeed = 0.02f;
    }
    return v;
}

}  // namespace

int main() {
    // the straight: nothing to feel
    {
        const input::HapticsOut o = input::haptics(cruising(), {1.0f, true});
        expect(o.low == 0.0f && o.high == 0.0f && o.triggerLeft == 0.0f && o.triggerRight == 0.0f &&
                   o.brakeResistance == 0.0f && o.throttlePulse == 0.0f,
               "straight: no output");
    }
    // a corner, the fronts sliding past the rears: the low motor, growing with it, nothing else
    {
        float last = -1.0f;
        bool rising = true;
        for (int f = 0; f < 10; ++f) {
            scene::Vehicle v = cruising();
            v.wheel[0].slipAngle = v.wheel[1].slipAngle = 0.04f + 0.012f * f;
            v.wheel[2].slipAngle = v.wheel[3].slipAngle = 0.03f;
            const input::HapticsOut o = input::haptics(v, {1.0f, true});
            rising &= o.low >= last && o.high == 0.0f && o.triggerLeft == 0.0f && o.triggerRight == 0.0f;
            last = o.low;
        }
        expect(rising && last > 0.4f, "understeer: the low motor rises with the front slip angle");
        scene::Vehicle balanced = cruising();
        for (scene::Wheel& w : balanced.wheel) w.slipAngle = 0.15f;   // all four sliding alike: a drift, not understeer
        expect(input::understeer(balanced) == 0.0f, "fronts and rears sliding alike: no understeer");
    }
    // a kerb: one wheel's stroke speed spikes for two frames
    {
        scene::Vehicle v = cruising();
        v.wheel[1].strokeSpeed = 0.6f;
        const input::HapticsOut hit = input::haptics(v, {1.0f, true});
        const input::HapticsOut after = input::haptics(cruising(), {1.0f, true});
        expect(hit.high > 0.8f && after.high == 0.0f, "kerb: the high motor in the kerb's frame, gone the frame after");
        v.valid &= ~scene::kVehWheelStroke;
        expect(input::haptics(v, {1.0f, true}).high == 0.0f, "no stroke speed in the record: no kerb");
    }
    // braking into lock-up: the brake's resistance first, then the lock-up (low motor, left trigger)
    {
        scene::Vehicle firm = cruising();
        firm.wheel[0].slipRatio = firm.wheel[1].slipRatio = 0.15f;   // hard braking, not locked
        const input::HapticsOut f = input::haptics(firm, {1.0f, true});
        expect(f.brakeResistance > 0.2f && f.triggerLeft == 0.0f && f.low == 0.0f, "firm braking: resistance, no lock-up");
        scene::Vehicle locked = cruising();
        locked.wheel[0].slipRatio = 0.7f;
        const input::HapticsOut l = input::haptics(locked, {1.0f, true});
        expect(l.triggerLeft > 0.9f && l.low > 0.9f && l.brakeResistance > 0.9f && l.triggerRight == 0.0f,
               "lock-up: the left trigger, the low motor, full resistance");
    }
    // wheelspin: the right trigger and the throttle's pulse
    {
        scene::Vehicle v = cruising();
        v.wheel[2].slipRatio = v.wheel[3].slipRatio = -0.45f;
        const input::HapticsOut o = input::haptics(v, {1.0f, true});
        expect(o.triggerRight > 0.8f && o.throttlePulse > 0.8f && o.low > 0.6f && o.triggerLeft == 0.0f,
               "wheelspin: the right trigger, the throttle's pulse, the low motor");
        const input::HapticsOut half = input::haptics(v, {0.5f, true});
        expect(std::fabs(half.triggerRight - o.triggerRight * 0.5f) < 1e-6f && std::fabs(half.low - o.low * 0.5f) < 1e-6f,
               "strength 50 %: everything halved");
        const input::HapticsOut off = input::haptics(v, {1.0f, false});
        expect(off.triggerRight == 0.0f && off.throttlePulse == 0.0f && off.low == o.low, "triggers off: motors only");
    }
    // each effect can be turned off on its own (input.haptics.understeer, .lockup, .wheelspin, .kerbs)
    {
        scene::Vehicle v = cruising();
        v.wheel[0].slipAngle = v.wheel[1].slipAngle = 0.15f;   // understeer
        v.wheel[2].slipAngle = v.wheel[3].slipAngle = 0.03f;
        v.wheel[1].strokeSpeed = 0.6f;                          // a kerb
        input::HapticsConfig c;
        expect(input::haptics(v, c).low > 0.5f && input::haptics(v, c).high > 0.8f, "toggles: both on");
        c.understeer = false;
        expect(input::haptics(v, c).low == 0.0f && input::haptics(v, c).high > 0.8f, "understeer off: no low motor");
        c.kerbs = false;
        expect(input::haptics(v, c).high == 0.0f, "kerbs off: no high motor");
        scene::Vehicle locked = cruising();
        locked.wheel[0].slipRatio = 0.7f;
        input::HapticsConfig noLock;
        noLock.lockUp = false;
        const input::HapticsOut l = input::haptics(locked, noLock);
        expect(l.low == 0.0f && l.triggerLeft == 0.0f && l.brakeResistance == 0.0f,
               "lock-up off: no low motor, no left trigger, no brake resistance");
        scene::Vehicle spinning = cruising();
        spinning.wheel[2].slipRatio = -0.45f;
        input::HapticsConfig noSpin;
        noSpin.wheelspin = false;
        const input::HapticsOut w = input::haptics(spinning, noSpin);
        expect(w.low == 0.0f && w.triggerRight == 0.0f && w.throttlePulse == 0.0f,
               "wheelspin off: no low motor, no right trigger, no throttle pulse");
    }
    // stopped, airborne, or a record without speed: nothing, whatever the slips say
    {
        scene::Vehicle v = cruising(0.5f);
        v.wheel[0].slipRatio = 0.8f;
        expect(input::haptics(v, {1.0f, true}).low == 0.0f, "stopped: nothing");
        scene::Vehicle air = cruising();
        for (scene::Wheel& w : air.wheel) {
            w.onGround = 0;
            w.slipRatio = -0.9f;
        }
        expect(input::haptics(air, {1.0f, true}).low == 0.0f, "no wheel on the ground: nothing");
        scene::Vehicle unknown = cruising();
        unknown.valid = 0;
        unknown.wheel[0].slipRatio = 0.8f;
        expect(input::haptics(unknown, {1.0f, true}).low == 0.0f, "fields not filled: nothing");
        scene::Vehicle nan = cruising(std::nanf(""));
        nan.wheel[0].slipRatio = 0.8f;
        expect(input::haptics(nan, {1.0f, true}).low == 0.0f, "a NaN speed: nothing");
    }
    if (failures) return 1;
    std::printf("input_haptics: all checks passed\n");
    return 0;
}
