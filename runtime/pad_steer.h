#pragma once
// input.steer.game_band (docs/modern/spikes/D12-steering-band.md): GT4's steer command is 0 for left-stick bytes
// within about 29 % of a side of the centre (90-165), between its linear steer table and the car. modern compensates
// that band while a race's physics runs (the host's kOptionSteerBand, g in permille): a byte d off the centre becomes
// floor(g * 127 + (1 - g) * d) off it on the right and floor(g * 128 + (1 - g) * d) on the left (the pad's own
// scales), so the first byte off the centre lands on the band's edge at 29 %: 165 on the right, the band's last zero
// byte, so the next byte turns the wheels (no overshoot into a step); 91 on the left, a byte or two inside the band.
// Each further byte moves the output by one at most, and full stick stays 0 and 255. The centre stays the centre.
// Header only (tests/pad_steer.cpp).
#include <cstdint>

namespace gt4::rt::pad {

constexpr uint8_t steer_band(uint8_t byte, uint32_t permille) {
    if (permille == 0 || byte == 128) return byte;
    if (permille > 1000) permille = 1000;
    const bool right = byte > 128;
    const uint32_t d = right ? byte - 128u : 128u - byte;   // 1..127 right, 1..128 left
    const uint32_t scale = right ? 127u : 128u;
    // in 1/1000 of a byte: g * scale + (1 - g) * d, floored
    const uint32_t out = (permille * scale + (1000u - permille) * d) / 1000u;
    return right ? static_cast<uint8_t>(128u + out) : static_cast<uint8_t>(128u - out);
}

}  // namespace gt4::rt::pad
