#include "wheel_physics.h"

#include "haptics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gt4::input {

namespace {

namespace scene = gfx::scene;

constexpr float kLowSpeed = 10.0f / 3.6f;   // m/s: below it the spring and damper fade in

float ramp(float x, float from, float span) { return std::clamp((x - from) / span, 0.0f, 1.0f); }

bool has(const scene::Vehicle& v, uint16_t fields) { return (v.valid & fields) == fields; }

int16_t level(float x) { return static_cast<int16_t>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f)); }

}  // namespace

float aligningTorque(const scene::Vehicle& v, float alphaPeak) {
    if (!has(v, scene::kVehWheelForces | scene::kVehWheelSlip | scene::kVehWheelContact)) return 0.0f;
    if (!(alphaPeak > 0.0f)) return 0.0f;
    float mz = 0.0f, grip = 0.0f;
    for (int i = 0; i < 2; ++i) {   // FL, FR
        const scene::Wheel& w = v.wheel[i];
        if (!w.onGround || !std::isfinite(w.forceLateral) || !std::isfinite(w.slipAngle) || !(w.load > 0.0f))
            continue;
        const float trail = std::max(0.0f, 1.0f - std::fabs(w.slipAngle) / alphaPeak);
        mz += w.forceLateral * trail;
        grip += w.load;   // (mu 1.0: GT4's lateral forces reach about the load at the limit, R14)
    }
    return grip > 0.0f ? std::clamp(mz / grip, -1.0f, 1.0f) : 0.0f;
}

float kerbVibration(const scene::Vehicle& v) {
    if (!has(v, scene::kVehWheelStroke | scene::kVehWheelContact)) return 0.0f;
    float m = 0.0f;
    for (int i = 0; i < 2; ++i)
        if (v.wheel[i].onGround && std::isfinite(v.wheel[i].strokeSpeed))
            m = std::max(m, std::fabs(v.wheel[i].strokeSpeed));
    return ramp(m, 0.15f, 0.35f);   // D14's roadTexture: 0.15 m/s starts it, 0.5 is full
}

bool kerbEvent(const scene::Vehicle& v) {
    if (!has(v, scene::kVehWheelStroke | scene::kVehWheelContact)) return false;
    for (int i = 0; i < 2; ++i)
        if (v.wheel[i].onGround && std::fabs(v.wheel[i].strokeSpeed) > 0.15f) return true;
    return false;
}

float slipVibration(const scene::Vehicle& v) { return std::max(lockUp(v), wheelspin(v)); }

float lowSpeed(const scene::Vehicle& v) {
    if (!has(v, scene::kVehVelocity) || !std::isfinite(v.speed)) return 0.0f;
    return 1.0f - ramp(v.speed, 0.0f, kLowSpeed);
}

WheelForces physicsForces(const scene::Vehicle& v, const PhysicsFfbConfig& c) {
    WheelForces out;
    bool grounded = false;
    for (const scene::Wheel& w : v.wheel) grounded |= w.onGround != 0;
    if (!has(v, scene::kVehVelocity) || !grounded) return out;   // (in the air, or no record: nothing to feel)
    const float strength = std::clamp(c.strength, 0.0f, 1.0f);
    const float budget = std::min(strength, kWheelForceCeiling);   // the sum's limit, as a share of full scale
    const float mz = aligningTorque(v, c.alphaPeak) * (c.mzSign < 0 ? -1.0f : 1.0f);
    const float constant = std::clamp(mz * strength, -budget, budget);
    out.constantOn = true;
    out.constantLevel = level(constant);
    // the vibration: kerbs or slip, the larger; its period follows the speed (a kerb's ridges pass faster)
    const float kerb = kerbVibration(v), slip = slipVibration(v);
    const float vibration = std::max(kerb, slip) * 0.5f * strength;   // (half scale: a texture, not a force)
    const float room = std::max(0.0f, budget - std::fabs(constant));   // the vibration gives way first
    const float amplitude = std::min(vibration, room);
    if (amplitude > 0.0f) {
        out.periodicOn = true;
        out.periodicMagnitude = level(amplitude);
        const float speed = std::isfinite(v.speed) ? std::max(v.speed, 1.0f) : 1.0f;
        out.periodicPeriodMs = static_cast<uint16_t>(
            kerb >= slip ? std::clamp(std::lround(600.0f / speed), 16L, 120L) : 25L);   // slip: a 40 Hz buzz
    }
    // low speed: a centring spring and a damper fade in (both passive: the ceiling is their saturation)
    const float slow = lowSpeed(v);
    if (slow > 0.0f) {
        const auto condition = [&](float share) {
            WheelCondition w;
            const float s = std::min(share * slow * strength, kWheelForceCeiling);
            w.rightCoeff = w.leftCoeff = static_cast<int16_t>(std::lround(s * 32767.0f));
            const float saturation = std::min(strength, kWheelForceCeiling);
            w.rightSat = w.leftSat = static_cast<uint16_t>(std::lround(saturation * 65535.0f));
            return w;
        };
        out.springOn = true;
        out.spring = condition(0.5f);
        out.damperOn = true;
        out.damper = condition(0.3f);
    }
    return out;
}

}  // namespace gt4::input
