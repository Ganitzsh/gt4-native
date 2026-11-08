// Steering and pedal response (response.h).
#include "response.h"

#include <algorithm>
#include <cmath>

namespace gt4::input {

float shapeMagnitude(float x, const Shape& shape) {
    const float a = std::clamp(x, 0.0f, 1.0f);
    if (a <= shape.deadzone) return 0.0f;
    const float span = 1.0f - shape.deadzone - shape.outer;
    float t = span > 0.0f ? std::min((a - shape.deadzone) / span, 1.0f) : 1.0f;
    if (shape.linearity != 1.0f) t = std::pow(t, shape.linearity);
    return std::min(t * shape.sensitivity, 1.0f) * shape.saturation;
}

float shapeAxis(float v, const Shape& shape) {
    const float m = shapeMagnitude(std::fabs(v), shape);
    return v < 0.0f ? -m : m;
}

float smooth(float current, float target, float tauMs, float dt) {
    if (tauMs <= 0.0f) return target;
    const float alpha = 1.0f - std::exp(-std::max(dt, 0.0f) * 1000.0f / tauMs);
    return current + (target - current) * alpha;
}

float ramp(float current, float target, float rateMs, float dt) {
    if (rateMs <= 0.0f) return target;
    const float stepOut = std::max(dt, 0.0f) * 1000.0f / rateMs;   // travel a step, away from the centre
    if (target > current) {
        if (current < 0.0f) return std::min(current + 2.0f * stepOut, std::min(target, 0.0f));   // back to the centre
        return std::min(current + stepOut, target);
    }
    if (target < current) {
        if (current > 0.0f) return std::max(current - 2.0f * stepOut, std::max(target, 0.0f));
        return std::max(current - stepOut, target);
    }
    return current;
}

namespace {

// An action's bindings split as the response needs them: the digital ones (keys and buttons, summed for an axis,
// the largest for a pedal) and the analogue ones (axes, raw: without the binding's dead zone).
struct Parts {
    float digital = 0.0f;
    float analogue = 0.0f;
};

Parts parts(const std::vector<Binding>& bindings, const Snapshot& s, bool axis) {
    Parts p;
    for (const Binding& b : bindings) {
        if (b.source != Source::Key && !s.gamepad) continue;
        switch (b.source) {
        case Source::Key:
        case Source::Button: {
            const bool down = b.source == Source::Key ? s.keys[b.code] : s.buttons[b.code];
            const float x = down ? b.scale : 0.0f;
            p.digital = axis ? p.digital + x : std::max(p.digital, std::clamp(x, 0.0f, 1.0f));
            break;
        }
        case Source::Axis: {
            const float x = s.axes[b.code] * b.scale;
            p.analogue = axis ? p.analogue + x : std::max(p.analogue, std::clamp(x, 0.0f, 1.0f));
            break;
        }
        }
    }
    return p;
}

}  // namespace

void Response::step(ps2::PadInput& in, const Snapshot& s, const ResponseConfig& config, float dt) {
    if (!config.modern) return;
    // steering: the keys ramped, then summed with the stick, shaped, smoothed
    const Parts steer = parts(map_.bindings[kLeftX], s, true);
    keySteer_ = ramp(keySteer_, std::clamp(steer.digital, -1.0f, 1.0f), config.keyRateMs, dt);
    const float steerTotal = std::clamp(keySteer_ + steer.analogue, -1.0f, 1.0f);
    steer_ = smooth(steer_, shapeAxis(steerTotal, config.steer), config.steerSmoothingMs, dt);
    in.leftX = steer_;   // (GT4's band is compensated game-side while a race runs: runtime/pad_steer.h)
    // the pedals: L2 and R2's pressure (a key or button reads full; a trigger is shaped), smoothed
    const ps2::PadButton pedal[2] = {ps2::PadL2, ps2::PadR2};
    for (int k = 0; k < 2; ++k) {
        const Parts p = parts(map_.bindings[pedal[k]], s, false);
        pedals_[k] = smooth(pedals_[k], std::max(p.digital, shapeMagnitude(p.analogue, config.pedal)),
                            config.pedalSmoothingMs, dt);
        in.button[pedal[k]] = pedals_[k];
    }
    // the right stick's halves (gas and brake in GT4's analog setup), each shaped as a pedal
    const Parts right = parts(map_.bindings[kRightY], s, true);
    rightY_ = smooth(rightY_, shapeAxis(std::clamp(right.digital + right.analogue, -1.0f, 1.0f), config.pedal),
                     config.pedalSmoothingMs, dt);
    in.rightY = rightY_;
}

}  // namespace gt4::input
