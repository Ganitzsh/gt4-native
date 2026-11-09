#include "wheel_ffb.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gt4::input {

namespace {

int32_t word(const uint8_t* block, int offset) {
    int32_t v;
    std::memcpy(&v, block + offset, 4);
    return v;
}

// The share of full scale a 0..255 value is played at: the strength, and never past the ceiling.
float share(int32_t value, float strength) {
    const float v = std::clamp(static_cast<float>(value), 0.0f, 255.0f) / 255.0f;
    return std::min(v * std::clamp(strength, 0.0f, 1.0f), kWheelForceCeiling);
}

WheelCondition condition(const uint8_t* block, float strength) {
    WheelCondition c;   // (center 0: wheel_ffb.h, +0x0C)
    c.rightCoeff = static_cast<int16_t>(std::lround(share(word(block, 0x14), strength) * 32767.0f));
    c.leftCoeff = static_cast<int16_t>(std::lround(share(word(block, 0x18), strength) * 32767.0f));
    c.rightSat = static_cast<uint16_t>(std::lround(share(word(block, 0x1C), strength) * 65535.0f));
    c.leftSat = static_cast<uint16_t>(std::lround(share(word(block, 0x20), strength) * 65535.0f));
    return c;
}

}  // namespace

WheelEffectKind wheelEffectKind(uint8_t type) {
    switch (type) {
    case 0x00: return WheelEffectKind::Constant;
    case 0x07: return WheelEffectKind::Spring;
    case 0x10: return WheelEffectKind::Damper;
    default: return WheelEffectKind::None;
    }
}

int32_t gt4ConstantMagnitude(const Gt4WheelFx& fx) {
    const uint32_t count = std::min<uint32_t>(fx.count, GT4_WHEEL_FX_EFFECTS);
    for (uint32_t i = 0; i < count; ++i)
        if (fx.effects[i].state == GT4_WHEEL_FX_RUNNING &&
            wheelEffectKind(fx.effects[i].block[0]) == WheelEffectKind::Constant)
            return std::clamp(word(fx.effects[i].block, 0x0C), -255, 255);
    return 0;
}

WheelForces wheelForces(const Gt4WheelFx& fx, const WheelFfbConfig& config) {
    WheelForces out;
    bool seenConstant = false, seenSpring = false, seenDamper = false;
    const uint32_t count = std::min<uint32_t>(fx.count, GT4_WHEEL_FX_EFFECTS);
    for (uint32_t i = 0; i < count; ++i) {
        const Gt4WheelEffect& e = fx.effects[i];
        if (e.state == GT4_WHEEL_FX_NONE) continue;
        const bool running = e.state == GT4_WHEEL_FX_RUNNING;
        switch (wheelEffectKind(e.block[0])) {
        case WheelEffectKind::Constant: {
            if (seenConstant) break;
            seenConstant = true;
            const int32_t magnitude = std::clamp(word(e.block, 0x0C), -255, 255);
            const float level = share(std::abs(magnitude), config.strength) * 32767.0f;
            const int sign = (magnitude < 0 ? -1 : 1) * (config.constantSign < 0 ? -1 : 1);
            out.constantOn = running;
            out.constantLevel = static_cast<int16_t>(sign * std::lround(level));
            break;
        }
        case WheelEffectKind::Spring:
            if (seenSpring) break;
            seenSpring = true;
            out.springOn = running;
            out.spring = condition(e.block, config.strength);
            out.springCentreRaw = word(e.block, 0x0C);
            break;
        case WheelEffectKind::Damper:
            if (seenDamper) break;
            seenDamper = true;
            out.damperOn = running;
            out.damper = condition(e.block, config.strength);
            break;
        case WheelEffectKind::None:
            break;
        }
    }
    return out;
}

}  // namespace gt4::input
