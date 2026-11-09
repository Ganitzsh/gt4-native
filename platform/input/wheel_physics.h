#pragma once
// A wheel's force feedback from GT4's physics (PLAN.md D8): the player's car's Vehicle record (CONTRACTS.md a.4,
// D6) to the forces D7's WheelHaptics plays, for D15's native wheel mode. Until D15 it is reached only through
// GT4_PHYSICS_FFB=1 (main.cpp), in place of GT4's own effects. No SDL here: tests/input_wheel_physics.cpp.
//
// - Aligning torque (the constant force): Mz is the front wheels' sum of forceLateral x t(slipAngle), with a
//   pneumatic trail t = max(0, 1 - |alpha| / alphaPeak) that falls to nothing as the front tyres saturate (the loss
//   of weight at understeer), normalised by the front axle's grip (its loads x 1.0) so it runs -1..1 whatever the
//   car. Its sign against GT4's own A (D7's convention) is mzSign; D8's lap comparison settles it, and fits
//   alphaPeak too. steeringTorque isn't filled (R14 open 1), so the tyre forces are the source.
// - Kerbs and road texture: a vibration from the front wheels' stroke speed (D14's roadTexture ramp, 0.15-0.5 m/s),
//   its period following the speed. Lock-up and wheelspin: a vibration from D14's lockUp and wheelspin. The two
//   share the one periodic effect, the larger and its period.
// - Low speed: a centring spring and a damper fading in below 10 km/h, where no aligning torque builds.
// - The strength scales all of it, and the constant force plus the vibration never exceed the strength and the
//   ceiling together (wheel_ffb.h's kWheelForceCeiling): the vibration gives way first.
#include "gt4/gfx.h"
#include "wheel_ffb.h"

namespace gt4::input {

struct PhysicsFfbConfig {
    float strength = 1.0f;     // input.wheel.ffb_strength, 0..1
    float alphaPeak = 0.15f;   // rad: the front slip angle where the trail is gone: GT4's front tyre force peaks
                               // there (|Fy| / load, 0.14-0.18 rad on D8's lap); GT4's own A has no fade to fit
    int mzSign = -1;           // Mz against GT4's A (D7's convention): opposite, on all of D8's lap's frames
};

// The parts, from one record (pure).
float aligningTorque(const gfx::scene::Vehicle& v, float alphaPeak);   // -1..1, before mzSign
float kerbVibration(const gfx::scene::Vehicle& v);                     // 0..1: the front wheels' stroke speed
float slipVibration(const gfx::scene::Vehicle& v);                     // 0..1: lock-up or wheelspin
float lowSpeed(const gfx::scene::Vehicle& v);                          // 0..1: 1 stopped, 0 from 10 km/h

// A front wheel's kerb event (PLAN D8's session test): on the ground, |strokeSpeed| over 0.15 m/s.
bool kerbEvent(const gfx::scene::Vehicle& v);

// One record to the forces (a record without the fields, or a car stopped in the air: all off).
WheelForces physicsForces(const gfx::scene::Vehicle& v, const PhysicsFfbConfig& config);

}  // namespace gt4::input
