// The action map (input_map.h).
#include "input_map.h"

#include <algorithm>
#include <cmath>

namespace gt4::input {

namespace {

Binding key(SDL_Scancode k, float scale = 1.0f) { return {Source::Key, k, scale, 0.0f}; }
Binding button(SDL_GamepadButton b) { return {Source::Button, b, 1.0f, 0.0f}; }
Binding axis(SDL_GamepadAxis a, float deadZone = 0.0f, float scale = 1.0f) { return {Source::Axis, a, scale, deadZone}; }

float axisValue(const Binding& b, const Snapshot& s) {
    const float v = s.axes[b.code];
    return (std::fabs(v) < b.deadZone ? 0.0f : v) * b.scale;
}

}  // namespace

Map originalMap() {
    using namespace ps2;
    Map m;
    m.name = "original";
    auto& a = m.bindings;
    // the keyboard (docs/GS_BACKEND.md, "Input")
    a[PadUp] = {key(SDL_SCANCODE_UP)};
    a[PadDown] = {key(SDL_SCANCODE_DOWN)};
    a[PadLeft] = {key(SDL_SCANCODE_LEFT)};
    a[PadRight] = {key(SDL_SCANCODE_RIGHT)};
    a[PadCross] = {key(SDL_SCANCODE_X)};
    a[PadCircle] = {key(SDL_SCANCODE_C)};
    a[PadSquare] = {key(SDL_SCANCODE_Z)};
    a[PadTriangle] = {key(SDL_SCANCODE_V)};
    a[PadL1] = {key(SDL_SCANCODE_Q)};
    a[PadR1] = {key(SDL_SCANCODE_E)};
    a[PadL2] = {key(SDL_SCANCODE_1)};
    a[PadR2] = {key(SDL_SCANCODE_3)};
    a[PadStart] = {key(SDL_SCANCODE_RETURN)};
    a[PadSelect] = {key(SDL_SCANCODE_BACKSPACE), key(SDL_SCANCODE_RSHIFT)};
    a[kLeftX] = {key(SDL_SCANCODE_J, -1.0f), key(SDL_SCANCODE_L, 1.0f)};
    a[kLeftY] = {key(SDL_SCANCODE_I, -1.0f), key(SDL_SCANCODE_K, 1.0f)};
    // the gamepad, in the standard layout: the PS2 pad's own places
    a[PadCross].push_back(button(SDL_GAMEPAD_BUTTON_SOUTH));
    a[PadCircle].push_back(button(SDL_GAMEPAD_BUTTON_EAST));
    a[PadSquare].push_back(button(SDL_GAMEPAD_BUTTON_WEST));
    a[PadTriangle].push_back(button(SDL_GAMEPAD_BUTTON_NORTH));
    a[PadSelect].push_back(button(SDL_GAMEPAD_BUTTON_BACK));
    a[PadStart].push_back(button(SDL_GAMEPAD_BUTTON_START));
    a[PadL3].push_back(button(SDL_GAMEPAD_BUTTON_LEFT_STICK));
    a[PadR3].push_back(button(SDL_GAMEPAD_BUTTON_RIGHT_STICK));
    a[PadL1].push_back(button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    a[PadR1].push_back(button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    a[PadUp].push_back(button(SDL_GAMEPAD_BUTTON_DPAD_UP));
    a[PadDown].push_back(button(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    a[PadLeft].push_back(button(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    a[PadRight].push_back(button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    // analogue triggers drive the L2/R2 pressures
    a[PadL2].push_back(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    a[PadR2].push_back(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    constexpr float kStickDeadZone = 0.08f;
    a[kLeftX].push_back(axis(SDL_GAMEPAD_AXIS_LEFTX, kStickDeadZone));
    a[kLeftY].push_back(axis(SDL_GAMEPAD_AXIS_LEFTY, kStickDeadZone));
    a[kRightX] = {axis(SDL_GAMEPAD_AXIS_RIGHTX, kStickDeadZone)};
    a[kRightY] = {axis(SDL_GAMEPAD_AXIS_RIGHTY, kStickDeadZone)};
    return m;
}

Map makeMap(const Options& o) {
    Map m = originalMap();
    if (!o.triggersAsPedals) return m;
    m.name = "triggers";
    for (auto* list : {&m.bindings[ps2::PadL2], &m.bindings[ps2::PadR2]})
        std::erase_if(*list, [](const Binding& b) { return b.source == Source::Axis; });
    // The gamepad buttons on the throttle and brake buttons take the triggers' places, R2 and L2 (a swap): GT4 shifts
    // on R2/L2 by default, which would otherwise have no gamepad binding left (docs/modern/spikes/D1-bindings.md).
    const auto moveButtons = [&](ps2::PadButton from, ps2::PadButton to) {
        if (from == to) return;
        auto& list = m.bindings[from];
        for (const Binding& b : list)
            if (b.source == Source::Button) m.bindings[to].push_back(b);
        std::erase_if(list, [](const Binding& b) { return b.source == Source::Button; });
    };
    moveButtons(o.throttle, ps2::PadR2);
    moveButtons(o.brake, ps2::PadL2);
    m.bindings[o.throttle].push_back(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    m.bindings[o.brake].push_back(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    return m;
}

namespace {
const char* const kButtonNames[ps2::PadButtonCount] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                                      "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};
}

bool buttonByName(const std::string& name, ps2::PadButton& out) {
    for (uint32_t b = 0; b < ps2::PadButtonCount; ++b)
        if (name == kButtonNames[b]) {
            out = static_cast<ps2::PadButton>(b);
            return true;
        }
    return false;
}

const char* buttonName(ps2::PadButton b) { return b < ps2::PadButtonCount ? kButtonNames[b] : "?"; }

ps2::PadInput apply(const Map& map, const Snapshot& s) {
    ps2::PadInput in;
    for (uint32_t action = 0; action < kActionCount; ++action) {
        const bool stick = action >= kLeftX;
        float v = 0.0f;
        for (const Binding& b : map.bindings[action]) {
            if (b.source != Source::Key && !s.gamepad) continue;
            float x = 0.0f;
            switch (b.source) {
            case Source::Key: x = s.keys[b.code] ? b.scale : 0.0f; break;
            case Source::Button: x = s.buttons[b.code] ? b.scale : 0.0f; break;
            case Source::Axis: x = axisValue(b, s); break;
            }
            v = stick ? v + x : std::max(v, std::clamp(x, 0.0f, 1.0f));
        }
        if (stick) v = std::clamp(v, -1.0f, 1.0f);
        switch (action) {
        case kLeftX: in.leftX = v; break;
        case kLeftY: in.leftY = v; break;
        case kRightX: in.rightX = v; break;
        case kRightY: in.rightY = v; break;
        default: in.button[action] = v; break;
        }
    }
    return in;
}

}  // namespace gt4::input
