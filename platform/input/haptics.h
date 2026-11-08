#pragma once
// Gamepad haptics from the physics (PLAN.md D14): what the player's car does, from its Vehicle record (CONTRACTS.md
// a.4, sent at each frame's end, A9 / D6), as motor and trigger values for the gamepad. GT4's own rumble (D2) stays and
// mixes in, the larger value per motor (the host does that).
//
// The conventions, from GT4's physics (D6's lap log, docs/modern/spikes/D12-steering-band.md's tools): a wheel's slip
// ratio is positive under braking (about +0.1 in hard braking, up to +0.8 locked) and negative under drive (about -0.06
// accelerating hard, down to -0.7 spinning); slip angles reach about 0.2 rad.
//   low-frequency motor: front slip angle past the rear's (understeer), lock-up, wheelspin;
//   high-frequency motor: kerbs and road texture, from the wheels' stroke speed when the record has it
//     (kVehWheelStroke); the surface ids are not known yet, so no kerb from the surface;
//   triggers: lock-up on the left (brake), wheelspin on the right (throttle); a DualSense's brake resistance rises as
//     the front wheels near lock-up.
// Nothing when the car is stopped or has no wheel on the ground. One frame's record gives one frame's output: no
// filtering, so the output is never more than the record's frame behind (D11). No SDL here: tests/input_haptics.cpp.
#include "gt4/gfx.h"

namespace gt4::input {

struct HapticsOut {
    float low = 0.0f, high = 0.0f;                 // the motors, 0..1
    float triggerLeft = 0.0f, triggerRight = 0.0f; // Xbox impulse triggers, 0..1
    float brakeResistance = 0.0f;                  // DualSense left trigger's resistance, 0..1
    float throttlePulse = 0.0f;                    // DualSense right trigger's vibration, 0..1
};

// The parts, each 0..1 from one record (pure).
float understeer(const gfx::scene::Vehicle& v);
float lockUp(const gfx::scene::Vehicle& v);
float wheelspin(const gfx::scene::Vehicle& v);
float nearLockUp(const gfx::scene::Vehicle& v);     // the front wheels' braking slip on its way to lock-up
float roadTexture(const gfx::scene::Vehicle& v);    // stroke speed (kVehWheelStroke), 0 without it

// What the player asked for (input.haptics.*): the strength (0..1), the trigger effects, and each effect on or off
// (lock-up off also leaves the brake's trigger effects out, wheelspin off the throttle's).
struct HapticsConfig {
    float strength = 1.0f;
    bool triggers = true;
    bool understeer = true, lockUp = true, wheelspin = true, kerbs = true;
};

// One record to the outputs.
HapticsOut haptics(const gfx::scene::Vehicle& v, const HapticsConfig& config);

}  // namespace gt4::input
