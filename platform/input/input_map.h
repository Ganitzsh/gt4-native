#pragma once
// The game's pads from the host's keyboard and gamepads, through an action map (PLAN.md D1).
//
// An action is one input of the PS2 pad GT4 reads (ps2_pad.h): a button with its pressure, or one stick axis.
// Each action has bindings: a key, a gamepad button, or a gamepad axis. A button action takes the largest of its
// bindings' values (0 or 1 for keys and buttons, the axis' travel for an axis); a stick action adds its bindings'
// values (keys -1 or +1, axes with a dead zone) and clamps the sum to -1 .. 1.
//
// The `original` map is the host's mapping before the map existed (main.cpp's readPad, docs/GS_BACKEND.md,
// "Input"): the same 18 bytes for every input (tests/input_map.cpp). No SDL call here: the state comes in as a
// Snapshot, so tests build one by hand.
#include "ps2_pad.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gt4::input {

// What the host read this frame from the keyboard and one gamepad.
struct Snapshot {
    bool keys[SDL_SCANCODE_COUNT] = {};
    bool gamepad = false;  // a gamepad is attached (its buttons and axes below)
    bool buttons[SDL_GAMEPAD_BUTTON_COUNT] = {};
    float axes[SDL_GAMEPAD_AXIS_COUNT] = {};  // SDL's value / 32767: sticks -1.00003 .. 1, triggers 0 .. 1
};

enum class Source : uint8_t { Key, Button, Axis };

struct Binding {
    Source source = Source::Key;
    int code = 0;          // SDL_Scancode, SDL_GamepadButton or SDL_GamepadAxis
    float scale = 1.0f;    // stick actions: the sign of a key, the direction of an axis
    float deadZone = 0.0f; // axes: |value| below it reads 0
};

// The pad's inputs: the 16 buttons (ps2::PadButton), then the four stick axes.
enum Action : uint32_t {
    kLeftX = ps2::PadButtonCount, kLeftY, kRightX, kRightY, kActionCount
};

struct Map {
    std::string name;
    std::vector<Binding> bindings[kActionCount];
};

Map originalMap();

// The settings that change the map (input.*; `original` is the PS2 mapping above and gets none of them).
struct Options {
    // input.pedals: `original` (the triggers give the L2/R2 pressures) or `triggers` (the right trigger gives the
    // pressure of the throttle button and the left one the brake's; the gamepad buttons on those two move to R2 and
    // L2, GT4's default shift up and down)
    bool triggersAsPedals = false;
    ps2::PadButton throttle = ps2::PadCross;  // input.throttle_button
    ps2::PadButton brake = ps2::PadSquare;    // input.brake_button
};
Map makeMap(const Options& options);

// "cross" -> PadCross, ...: the names ps2_pad.h and GT4_PAD_SCRIPT use; false for an unknown name
bool buttonByName(const std::string& name, ps2::PadButton& out);
const char* buttonName(ps2::PadButton b);

// The pad input of one pad from the snapshot (a pad with no gamepad still reads the keys bound).
ps2::PadInput apply(const Map& map, const Snapshot& state);

}  // namespace gt4::input
