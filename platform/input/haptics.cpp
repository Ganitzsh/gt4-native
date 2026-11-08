// Gamepad haptics from the physics (haptics.h).
#include "haptics.h"

#include <algorithm>
#include <cmath>

namespace gt4::input {

namespace {

namespace scene = gfx::scene;

float ramp(float x, float from, float span) { return std::clamp((x - from) / span, 0.0f, 1.0f); }

bool moving(const scene::Vehicle& v) {
    if (!(v.valid & scene::kVehVelocity) || !(v.valid & scene::kVehWheelSlip)) return false;
    if (!(v.speed > 1.0f)) return false;   // 3.6 km/h (also false for NaN)
    for (const scene::Wheel& w : v.wheel)
        if (w.onGround) return true;
    return false;
}

}  // namespace

float understeer(const scene::Vehicle& v) {
    if (!moving(v)) return 0.0f;
    const float front = (std::fabs(v.wheel[0].slipAngle) + std::fabs(v.wheel[1].slipAngle)) / 2;
    const float rear = (std::fabs(v.wheel[2].slipAngle) + std::fabs(v.wheel[3].slipAngle)) / 2;
    // the fronts sliding more than the rears, once they slide at all (past about 3 degrees)
    return ramp(front - rear, 0.03f, 0.08f) * ramp(front, 0.05f, 0.05f);
}

float lockUp(const scene::Vehicle& v) {
    if (!moving(v)) return 0.0f;
    float m = 0.0f;
    for (const scene::Wheel& w : v.wheel)
        if (w.onGround) m = std::max(m, ramp(w.slipRatio, 0.25f, 0.35f));   // hard braking reaches about 0.2
    return m;
}

float wheelspin(const scene::Vehicle& v) {
    if (!moving(v)) return 0.0f;
    float m = 0.0f;
    for (const scene::Wheel& w : v.wheel)
        if (w.onGround) m = std::max(m, ramp(-w.slipRatio, 0.15f, 0.35f));  // hard acceleration reaches about 0.09
    return m;
}

float nearLockUp(const scene::Vehicle& v) {
    if (!moving(v)) return 0.0f;
    const float front = std::max(v.wheel[0].slipRatio, v.wheel[1].slipRatio);
    return ramp(front, 0.08f, 0.17f);   // from firm braking to the edge of lock-up (0.25)
}

float roadTexture(const scene::Vehicle& v) {
    if (!moving(v) || !(v.valid & scene::kVehWheelStroke)) return 0.0f;
    float m = 0.0f;
    for (const scene::Wheel& w : v.wheel)
        if (w.onGround) m = std::max(m, std::fabs(w.strokeSpeed));
    return ramp(m, 0.15f, 0.5f);   // m/s of suspension movement: a kerb's or a rough surface's, not a road's ripple
}

HapticsOut haptics(const scene::Vehicle& v, const HapticsConfig& c) {
    HapticsOut out;
    const float s = std::clamp(c.strength, 0.0f, 1.0f);
    const float under = c.understeer ? understeer(v) : 0.0f;
    const float lock = c.lockUp ? lockUp(v) : 0.0f, spin = c.wheelspin ? wheelspin(v) : 0.0f;
    out.low = std::max({under * 0.6f, lock, spin * 0.8f}) * s;
    out.high = c.kerbs ? roadTexture(v) * s : 0.0f;
    if (c.triggers) {
        out.triggerLeft = lock * s;
        out.triggerRight = spin * s;
        out.brakeResistance = c.lockUp ? std::max(nearLockUp(v), lock) * s : 0.0f;
        out.throttlePulse = spin * s;
    }
    return out;
}

}  // namespace gt4::input
