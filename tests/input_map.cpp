// platform/input/input_map.cpp: the `original` map gives the 18 pad bytes the host's readPad (main.cpp, before the
// map) gave, for every key, button and axis alone, across each axis' range, and for random combinations.
#include "input_map.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

using namespace gt4;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

// main.cpp's readPad as it was (a80cfca, main.cpp:394-443), with the SDL reads replaced by the snapshot's.
ps2::PadInput legacyReadPad(const input::Snapshot& s) {
    using namespace ps2;
    PadInput in;
    auto press = [&](PadButton b, bool down) { if (down) in.button[b] = 1.0f; };
    const bool* keys = s.keys;
    press(PadUp, keys[SDL_SCANCODE_UP]);
    press(PadDown, keys[SDL_SCANCODE_DOWN]);
    press(PadLeft, keys[SDL_SCANCODE_LEFT]);
    press(PadRight, keys[SDL_SCANCODE_RIGHT]);
    press(PadCross, keys[SDL_SCANCODE_X]);
    press(PadCircle, keys[SDL_SCANCODE_C]);
    press(PadSquare, keys[SDL_SCANCODE_Z]);
    press(PadTriangle, keys[SDL_SCANCODE_V]);
    press(PadL1, keys[SDL_SCANCODE_Q]);
    press(PadR1, keys[SDL_SCANCODE_E]);
    press(PadL2, keys[SDL_SCANCODE_1]);
    press(PadR2, keys[SDL_SCANCODE_3]);
    press(PadStart, keys[SDL_SCANCODE_RETURN]);
    press(PadSelect, keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_RSHIFT]);
    if (keys[SDL_SCANCODE_J]) in.leftX -= 1.0f;
    if (keys[SDL_SCANCODE_L]) in.leftX += 1.0f;
    if (keys[SDL_SCANCODE_I]) in.leftY -= 1.0f;
    if (keys[SDL_SCANCODE_K]) in.leftY += 1.0f;
    if (s.gamepad) {
        auto button = [&](SDL_GamepadButton b) { return s.buttons[b]; };
        auto axis = [&](SDL_GamepadAxis a) { return s.axes[a]; };
        press(PadCross, button(SDL_GAMEPAD_BUTTON_SOUTH));
        press(PadCircle, button(SDL_GAMEPAD_BUTTON_EAST));
        press(PadSquare, button(SDL_GAMEPAD_BUTTON_WEST));
        press(PadTriangle, button(SDL_GAMEPAD_BUTTON_NORTH));
        press(PadSelect, button(SDL_GAMEPAD_BUTTON_BACK));
        press(PadStart, button(SDL_GAMEPAD_BUTTON_START));
        press(PadL3, button(SDL_GAMEPAD_BUTTON_LEFT_STICK));
        press(PadR3, button(SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        press(PadL1, button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        press(PadR1, button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        press(PadUp, button(SDL_GAMEPAD_BUTTON_DPAD_UP));
        press(PadDown, button(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        press(PadLeft, button(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
        press(PadRight, button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
        in.button[PadL2] = std::max(in.button[PadL2], std::clamp(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f, 1.0f));
        in.button[PadR2] = std::max(in.button[PadR2], std::clamp(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f, 1.0f));
        auto stick = [](float v) { return std::fabs(v) < 0.08f ? 0.0f : v; };
        in.leftX = std::clamp(in.leftX + stick(axis(SDL_GAMEPAD_AXIS_LEFTX)), -1.0f, 1.0f);
        in.leftY = std::clamp(in.leftY + stick(axis(SDL_GAMEPAD_AXIS_LEFTY)), -1.0f, 1.0f);
        in.rightX = stick(axis(SDL_GAMEPAD_AXIS_RIGHTX));
        in.rightY = stick(axis(SDL_GAMEPAD_AXIS_RIGHTY));
    }
    return in;
}

long mismatches = 0;
void same(const input::Map& map, const input::Snapshot& s, const char* what) {
    uint8_t a[18], b[18];
    ps2::encodePad(legacyReadPad(s), a);
    ps2::encodePad(input::apply(map, s), b);
    if (std::memcmp(a, b, 18) != 0) {
        if (mismatches++ < 10) {
            std::fprintf(stderr, "FAILED: %s:", what);
            for (int k = 0; k < 18; ++k) std::fprintf(stderr, " %02x/%02x", a[k], b[k]);
            std::fprintf(stderr, "\n");
        }
        ++failures;
    }
}

// SDL's raw axis values as the host reads them (value / 32767)
float raw(int v) { return static_cast<float>(v) / 32767.0f; }

}  // namespace

int main() {
    const input::Map map = input::originalMap();
    expect(map.name == "original", "the map's name");

    input::Snapshot none;
    same(map, none, "nothing pressed");
    for (bool pad : {false, true}) {
        // every key alone (bound or not)
        for (int k = 0; k < SDL_SCANCODE_COUNT; ++k) {
            input::Snapshot s;
            s.gamepad = pad;
            s.keys[k] = true;
            same(map, s, "one key");
        }
        // every button alone, with and without a gamepad
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            input::Snapshot s;
            s.gamepad = pad;
            s.buttons[b] = true;
            same(map, s, "one button");
        }
        // every axis alone, over all of SDL's values
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            for (int v = -32768; v <= 32767; ++v) {
                input::Snapshot s;
                s.gamepad = pad;
                s.axes[a] = raw(v);
                same(map, s, "one axis");
            }
    }
    // the keyboard's stick keys with the stick: every pair of key states against stick values
    for (int v = -32768; v <= 32767; v += 7)
        for (int keys = 0; keys < 16; ++keys) {
            input::Snapshot s;
            s.gamepad = true;
            s.keys[SDL_SCANCODE_J] = keys & 1;
            s.keys[SDL_SCANCODE_L] = keys & 2;
            s.keys[SDL_SCANCODE_I] = keys & 4;
            s.keys[SDL_SCANCODE_K] = keys & 8;
            s.axes[SDL_GAMEPAD_AXIS_LEFTX] = raw(v);
            s.axes[SDL_GAMEPAD_AXIS_LEFTY] = raw(-v - 1);
            same(map, s, "stick keys with the stick");
        }
    // random combinations: keys the map binds, buttons, axes at random values
    std::mt19937 rng(4);
    const int bound[] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_X,
                         SDL_SCANCODE_C, SDL_SCANCODE_Z, SDL_SCANCODE_V, SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_1,
                         SDL_SCANCODE_3, SDL_SCANCODE_RETURN, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_RSHIFT,
                         SDL_SCANCODE_J, SDL_SCANCODE_L, SDL_SCANCODE_I, SDL_SCANCODE_K};
    for (int n = 0; n < 200000; ++n) {
        input::Snapshot s;
        s.gamepad = rng() % 8 != 0;
        for (int k : bound) s.keys[k] = rng() % 4 == 0;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) s.buttons[b] = rng() % 4 == 0;
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            s.axes[a] = rng() % 3 == 0 ? 0.0f : raw(static_cast<int>(rng() % 65536) - 32768);
        same(map, s, "a random combination");
    }

    // the options: input.pedals=original is the original map; =triggers moves the triggers to the pedal buttons
    {
        input::Options o;
        const input::Map orig = input::makeMap(o);
        input::Snapshot s;
        s.gamepad = true;
        s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = raw(16384);
        s.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = raw(32767);
        uint8_t a[18], b[18];
        ps2::encodePad(input::apply(map, s), a);
        ps2::encodePad(input::apply(orig, s), b);
        expect(orig.name == "original" && std::memcmp(a, b, 18) == 0, "input.pedals=original is the original map");

        o.triggersAsPedals = true;
        const input::Map pedals = input::makeMap(o);
        ps2::encodePad(input::apply(pedals, s), b);
        // byte 1: bit 0 L2, 1 R2, 6 Cross, 7 Square (pressed = 0); pressures at 6 + (6 Cross, 7 Square, 10 L2, 11 R2)
        expect(pedals.name == "triggers", "the pedals map's name");
        expect(b[6 + 6] == 0x80 && !(b[1] & 0x40), "half the right trigger: cross at half pressure, pressed");
        expect(b[6 + 7] == 0xFF && !(b[1] & 0x80), "the left trigger: square at full pressure");
        expect(b[6 + 10] == 0 && b[6 + 11] == 0 && (b[1] & 0x03) == 0x03, "L2 and R2 left alone by the triggers");
        s.keys[SDL_SCANCODE_1] = true;
        ps2::encodePad(input::apply(pedals, s), b);
        expect(b[6 + 10] == 0xFF && !(b[1] & 0x01), "L2 still from its key");

        ps2::PadButton t{}, k{};
        expect(input::buttonByName("r2", t) && !input::buttonByName("pedal", k) && input::buttonByName("l2", k) &&
                   std::string(input::buttonName(ps2::PadCross)) == "cross",
               "button names");
        o.throttle = t;
        o.brake = k;
        s = {};
        s.gamepad = true;
        s.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = raw(32767);
        ps2::encodePad(input::apply(input::makeMap(o), s), b);
        expect(b[6 + 11] == 0xFF && b[6 + 6] == 0, "the throttle on another button (R2)");
    }

    if (failures) {
        std::fprintf(stderr, "%d failure(s), %ld byte mismatches\n", failures, mismatches);
        return 1;
    }
    std::printf("input map: all checks passed\n");
    return 0;
}
