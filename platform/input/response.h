#pragma once
// Steering and pedal response (PLAN.md D12): what the host does to a pad's sticks and pedals between the action map
// (input_map.h, apply()) and the game. GT4's own sticks have no dead zone and a linear curve (spikes/D1-bindings.md),
// so this is the whole response the player feels.
//
// `original` (input.response, the original preset) bypasses all of it: apply()'s bytes, with the binding's own 8 %
// cut and no rescale. `modern` recomputes the steering axis and the pedals from the snapshot, without that cut:
//   steering (left stick X): the digital keys ramp (key_rate), then the keys and the stick are summed and shaped per
//     side: the inner dead zone with rescale (the output starts at 0 at its edge), the outer dead zone, linearity,
//     sensitivity, saturation; then smoothing. The dead zones are per axis, on X only: right for steering, never to
//     be made radial;
//   the pedals (L2 and R2's pressure from the triggers, and the right stick's halves, which GT4's analog setup can use
//     for gas and brake): the dead zones, linearity and smoothing, the same way. GT4's own curve and
//     input.trigger_travel apply on top in the game, as before.
// The shaping is pure functions (tests/input_response.cpp); Response holds the state (the ramp, the filters) of one
// pad and is stepped once a frame with the frame's dt.
#include "input_map.h"

namespace gt4::input {

// One axis' shape, in fractions of travel (the settings give percent).
struct Shape {
    float deadzone = 0.05f;    // inner: below it 0, above it rescaled from 0
    float outer = 0.02f;       // full output this far before the end of travel
    float linearity = 1.0f;    // exponent: above 1, finer near the centre
    float sensitivity = 1.0f;  // gain before the limit: above 1, full output comes early
    float saturation = 1.0f;   // the most output the axis gives
};

struct ResponseConfig {
    bool modern = false;       // false: original, apply()'s bytes untouched
    Shape steer;
    Shape pedal{0.05f, 0.02f, 1.0f, 1.0f, 1.0f};
    float steerSmoothingMs = 0.0f;  // a one-pole filter's time constant; 0 = none
    float pedalSmoothingMs = 0.0f;
    float keyRateMs = 150.0f;       // a steering key: full lock in this time, back at twice the rate; 0 = a step
};

// |x| in 0 .. 1 through the dead zones, linearity, sensitivity and saturation (one side of an axis, or a pedal).
float shapeMagnitude(float x, const Shape& shape);
// A signed axis, per side: the sign kept, the magnitude shaped.
float shapeAxis(float v, const Shape& shape);
// One step of a one-pole filter towards `target` with time constant tauMs over dt seconds (tauMs 0: the target).
float smooth(float current, float target, float tauMs, float dt);
// One step of a key's ramp towards `target` (-1, 0 or 1 summed): full travel in rateMs, back towards 0 (or across it)
// at twice the rate; rateMs 0: the target at once.
float ramp(float current, float target, float rateMs, float dt);

class Response {
public:
    // `map` gives the bindings the steering and pedals are recomputed from (the keys apart from the axes).
    explicit Response(const Map& map) : map_(map) {}
    // The pad input from apply() for this frame's snapshot, reshaped in place when config.modern; dt in seconds.
    void step(ps2::PadInput& in, const Snapshot& s, const ResponseConfig& config, float dt);
    void reset() { keySteer_ = steer_ = rightY_ = 0.0f; pedals_[0] = pedals_[1] = 0.0f; }

private:
    const Map& map_;
    float keySteer_ = 0.0f;   // the keys' ramped steering
    float steer_ = 0.0f;      // the filtered steering output
    float pedals_[2] = {};    // L2, R2 filtered
    float rightY_ = 0.0f;     // the right stick's Y filtered
};

}  // namespace gt4::input
