#pragma once
// A wheel's force effects as GT4 sets them through LGDEV (PLAN.md D7; docs/modern/spikes/R15-lgdev-effects.md), to the
// effects the host plays on the wheel: GT4_REC_WHEEL_FX's blocks to a constant force, a spring and a damper, each in
// SDL_Haptic's units, with the player's strength and the hard ceiling applied. No SDL here: tests/input_wheel_ffb.cpp.
//
// The 60-byte block (32-bit words, little-endian; the type a byte at +0):
//   +0x00 type: 0 a constant force (GT4's effect A), 0x10 a damper (B), 7 a spring (C)
//   +0x04 duration, -1 (endless); +0x08 0
//   +0x0C the constant force's magnitude (-255..255), or the spring's centre offset. The offset is 0 in every race
//         seen and its scale isn't known: read as SDL's units, a value in another scale (DirectInput's
//         -10,000..10,000) would centre the spring off the wheel's middle, a pull away from centre. So the spring is
//         played centred and the raw value reported (springCentreRaw) for the host to log, until a race shows one
//         (lane C).
//   +0x10 the constant force's direction, 90 (the wheel's axis)
//   +0x14, +0x18 the condition's positive and negative coefficients (0..255)
//   +0x1C, +0x20 its positive and negative saturations (0..255)
// The order of the conditions' four words is DirectInput's DICONDITION's (coefficients, then saturations), which the
// damper's values in a race fit: 32 and 32, then 128 and 255.
#include "gt4/link/protocol.h"

#include <cstdint>

namespace gt4::input {

// Every level and coefficient the host plays is at most this share of the device's full scale, whatever the
// strength (PLAN.md D7: until the live check has measured a wheel).
inline constexpr float kWheelForceCeiling = 0.60f;

// The constant force's length (ms): renewed while it runs, every kWheelConstantRenewMs and on each change, so a host
// that stalls lets it lapse at the device within 0.2 s (PLAN.md D7's hung-host case), while an ordinary hitch (a
// shader compile, streaming) holds the last force, as the PS2's wheel did; and a fast display doesn't start it every
// frame (lane C, the architect).
inline constexpr uint32_t kWheelConstantLengthMs = 200;
inline constexpr uint32_t kWheelConstantRenewMs = 30;

enum class WheelEffectKind : uint8_t { None, Constant, Spring, Damper };

// GT4's block type to the effect it is (None: a type D7 doesn't know).
WheelEffectKind wheelEffectKind(uint8_t type);

// One condition (spring or damper), in SDL_HapticCondition's units for its one axis.
struct WheelCondition {
    int16_t center = 0;
    int16_t rightCoeff = 0, leftCoeff = 0;        // 0..32767
    uint16_t rightSat = 0, leftSat = 0;            // 0..65535
    bool operator==(const WheelCondition&) const = default;
};

// What the host plays now: each effect on or off and its parameters.
struct WheelForces {
    bool constantOn = false, springOn = false, damperOn = false;
    int16_t constantLevel = 0;                     // -32767..32767, the sign as `constantSign` sets it
    WheelCondition spring, damper;            // the spring's center always 0 (above)
    int32_t springCentreRaw = 0;              // GT4's centre offset, as sent (not played)
    // A vibration (SDL_HAPTIC_SINE): D8's kerbs and slip (wheel_physics.h); never set from GT4's effects
    bool periodicOn = false;
    int16_t periodicMagnitude = 0;                 // 0..32767
    uint16_t periodicPeriodMs = 0;
    bool operator==(const WheelForces&) const = default;
};

// The player's side: the strength (input.wheel.ffb_strength, 0..1), and the sign that turns GT4's magnitude into the
// device's direction (+1 or -1: D7's first acceptance check sets it).
struct WheelFfbConfig {
    float strength = 1.0f;
    int constantSign = 1;
};

// GT4_REC_WHEEL_FX's effects to the forces: each effect by its block's type, running ones on. A second effect of a
// kind already taken is ignored (GT4 downloads one of each).
WheelForces wheelForces(const Gt4WheelFx& fx, const WheelFfbConfig& config);

// GT4's constant force as it set it (effect A: its +0x0C, -255..255; 0 when it has none or doesn't run it), for D8's
// comparison with the physics' aligning torque (PLAN.md D8).
int32_t gt4ConstantMagnitude(const Gt4WheelFx& fx);

// All off (the stops: PLAN.md D7).
inline WheelForces wheelForcesOff() { return {}; }

}  // namespace gt4::input
