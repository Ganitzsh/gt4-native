// platform/input/wheel_physics.cpp (PLAN.md D8): the formula's shape on synthetic records. The aligning torque follows
// the front lateral force's sign and size below alphaPeak and fades past it at a constant force (the loss of weight at
// understeer); nothing in the air or without the record's fields; kerbs and slip give a vibration, kerbEvent fires
// past 0.15 m/s on the ground only; the low-speed spring and damper fade out by 10 km/h; and the constant force plus
// the vibration never exceed the strength and the 60 % ceiling together, the vibration giving way first.
#include "wheel_physics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace gt4;
using namespace gt4::input;
namespace scene = gt4::gfx::scene;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

// A car at speed, four wheels down, the fronts carrying `fy` each at slip angle `alpha`, 4 kN each.
scene::Vehicle cornering(float fy, float alpha, float speed = 25.0f) {
    scene::Vehicle v{};
    v.prefix = {scene::kVehicle, scene::kVersion};
    v.valid = scene::kVehTransform | scene::kVehVelocity | scene::kVehWheelSlip | scene::kVehWheelForces |
              scene::kVehWheelContact | scene::kVehWheelStroke;
    v.speed = speed;
    for (scene::Wheel& w : v.wheel) {
        w.load = 4000.0f;
        w.onGround = 1;
        w.slipRatio = -0.01f;
        w.slipAngle = 0.01f;
    }
    for (int i = 0; i < 2; ++i) {
        v.wheel[i].forceLateral = fy;
        v.wheel[i].slipAngle = alpha;
    }
    return v;
}

int absLevel(int16_t x) { return std::abs(static_cast<int>(x)); }

}  // namespace

int main() {
    const PhysicsFfbConfig full{1.0f, 0.15f, 1};
    // the sign and size below alphaPeak: 2 x 3000 N x (1 - 0.05 / 0.15) over 8000 N of front load = 0.5
    expect(std::fabs(aligningTorque(cornering(3000.0f, 0.05f), 0.15f) - 0.5f) < 1e-4f, "Mz 0.5 at 3 kN, 0.05 rad");
    expect(aligningTorque(cornering(-3000.0f, 0.05f), 0.15f) < -0.49f, "Mz follows Fy's sign");
    expect(aligningTorque(cornering(1500.0f, 0.05f), 0.15f) < aligningTorque(cornering(3000.0f, 0.05f), 0.15f),
           "Mz grows with Fy");
    // the fade past the peak at a constant lateral force
    {
        const float a = aligningTorque(cornering(3000.0f, 0.05f), 0.15f);
        const float b = aligningTorque(cornering(3000.0f, 0.10f), 0.15f);
        const float c = aligningTorque(cornering(3000.0f, 0.15f), 0.15f);
        const float d = aligningTorque(cornering(3000.0f, 0.25f), 0.15f);
        expect(a > b && b > c && c == 0.0f && d == 0.0f, "Mz fades to nothing at alphaPeak and stays there");
        expect(aligningTorque(cornering(3000.0f, 0.10f), 0.20f) > b, "a later alphaPeak fades later");
    }
    // the sign setting flips the constant force only
    {
        const WheelForces p = physicsForces(cornering(3000.0f, 0.05f), full);
        const WheelForces n = physicsForces(cornering(3000.0f, 0.05f), {1.0f, 0.15f, -1});
        expect(p.constantOn && p.constantLevel == -n.constantLevel && p.constantLevel > 0, "mzSign flips A");
    }
    // nothing without the fields, or in the air
    {
        scene::Vehicle none = cornering(3000.0f, 0.05f);
        none.valid = 0;
        expect(physicsForces(none, full) == WheelForces{}, "no fields: all off");
        scene::Vehicle air = cornering(3000.0f, 0.05f);
        for (scene::Wheel& w : air.wheel) w.onGround = 0;
        expect(physicsForces(air, full) == WheelForces{}, "in the air: all off");
        scene::Vehicle nan = cornering(std::nanf(""), 0.05f);
        expect(aligningTorque(nan, 0.15f) == 0.0f, "a NaN force adds nothing");
    }
    // kerbs: the vibration, and the event's threshold
    {
        scene::Vehicle kerb = cornering(0.0f, 0.0f);
        kerb.wheel[0].strokeSpeed = 0.5f;
        expect(kerbEvent(kerb) && kerbVibration(kerb) == 1.0f, "a 0.5 m/s stroke: an event, full vibration");
        const WheelForces f = physicsForces(kerb, full);
        expect(f.periodicOn && f.periodicMagnitude > 0 && f.periodicPeriodMs >= 16 && f.periodicPeriodMs <= 120,
               "a kerb plays a vibration, its period from the speed");
        kerb.wheel[0].strokeSpeed = 0.14f;
        expect(!kerbEvent(kerb) && kerbVibration(kerb) == 0.0f, "under 0.15 m/s: no event, no vibration");
        kerb.wheel[0].strokeSpeed = 0.5f;
        kerb.wheel[0].onGround = 0;
        expect(!kerbEvent(kerb), "a wheel in the air makes no kerb event");
        scene::Vehicle rear = cornering(0.0f, 0.0f);
        rear.wheel[2].strokeSpeed = 0.5f;
        expect(!kerbEvent(rear) && kerbVibration(rear) == 0.0f, "the rear wheels' strokes are not the steering's");
    }
    // slip: lock-up gives the 40 Hz buzz
    {
        scene::Vehicle lock = cornering(0.0f, 0.0f);
        for (scene::Wheel& w : lock.wheel) w.slipRatio = 0.7f;
        const WheelForces f = physicsForces(lock, full);
        expect(slipVibration(lock) > 0.9f && f.periodicOn && f.periodicPeriodMs == 25, "lock-up: a 25 ms vibration");
    }
    // low speed: the spring and damper fade out by 10 km/h
    {
        const WheelForces stopped = physicsForces(cornering(0.0f, 0.0f, 0.0f), full);
        const WheelForces slow = physicsForces(cornering(0.0f, 0.0f, 1.4f), full);
        const WheelForces fast = physicsForces(cornering(0.0f, 0.0f, 3.0f), full);
        expect(stopped.springOn && stopped.damperOn && stopped.spring.rightCoeff > slow.spring.rightCoeff &&
                   slow.spring.rightCoeff > 0, "the spring fades with speed");
        expect(!fast.springOn && !fast.damperOn, "from 10 km/h: no spring, no damper");
    }
    // the sum: the constant force plus the vibration within the strength and the ceiling, at the limit of grip on a
    // kerb, at every strength
    for (float strength : {0.15f, 0.5f, 1.0f, 3.0f}) {
        scene::Vehicle worst = cornering(6000.0f, 0.01f);   // Mz past 1 before the clamp
        worst.wheel[0].strokeSpeed = worst.wheel[1].strokeSpeed = 0.8f;
        const WheelForces f = physicsForces(worst, {strength, 0.15f, 1});
        const float budget = std::fmin(std::fmin(strength, 1.0f), kWheelForceCeiling) * 32767.0f;
        char what[96];
        std::snprintf(what, sizeof what, "the sum within strength and ceiling at strength %.2f", strength);
        expect(absLevel(f.constantLevel) + (f.periodicOn ? f.periodicMagnitude : 0) <= budget + 1.0f, what);
        expect(f.spring.rightCoeff <= kWheelForceCeiling * 32767.0f + 1.0f, "the spring within the ceiling");
    }
    // PLAN D7's budget, the conditions included, at low speed where they fade in: on a kerb at 5 km/h with the
    // fronts loaded, at every strength, the constant force, the vibration and the spring's and the damper's largest
    // saturations together within min(strength, ceiling)
    for (float strength : {0.15f, 0.5f, 1.0f, 3.0f})
        for (float speed : {0.0f, 0.7f, 1.4f, 2.5f}) {
            scene::Vehicle slow = cornering(5000.0f, 0.02f, speed);
            slow.wheel[0].strokeSpeed = 0.8f;
            const WheelForces f = physicsForces(slow, {strength, 0.15f, 1});
            const float sum = absLevel(f.constantLevel) / 32767.0f +
                              (f.periodicOn ? f.periodicMagnitude / 32767.0f : 0.0f) +
                              std::max(f.spring.rightSat, f.spring.leftSat) / 65535.0f +
                              std::max(f.damper.rightSat, f.damper.leftSat) / 65535.0f;
            char what[112];
            std::snprintf(what, sizeof what, "the budget with the conditions at %.1f m/s, strength %.2f: %.4f", speed,
                          strength, sum);
            expect(sum <= std::fmin(std::fmin(strength, 1.0f), kWheelForceCeiling) + 1e-4f, what);
        }
    {   // stopped and unloaded: the conditions get the whole budget, the spring first
        const WheelForces f = physicsForces(cornering(0.0f, 0.0f, 0.0f), full);
        expect(f.springOn && f.spring.rightSat > 0 && f.damperOn && f.damper.rightSat > 0,
               "stopped: the spring and the damper both in the budget");
    }
    {   // the vibration gives way first: a moderate Mz keeps its level on a kerb
        scene::Vehicle v = cornering(2000.0f, 0.02f);
        const int16_t alone = physicsForces(v, full).constantLevel;
        v.wheel[0].strokeSpeed = 0.8f;
        expect(physicsForces(v, full).constantLevel == alone, "a kerb never takes from the aligning torque");
    }
    if (failures) return 1;
    std::printf("input_wheel_physics: all checks passed\n");
    return 0;
}
