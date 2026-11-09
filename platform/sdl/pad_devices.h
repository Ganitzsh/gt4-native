#pragma once
// The host's input devices for the game's two pads (PLAN.md D13; platform/input/devices.h): the gamepads open and the
// ports they hold (a device keeps its port across a reconnect), each device's profile from input_profiles.ini (its
// bindings to the PS2 pad), and each port's response (D12). Pad 1 has the keyboard too.
//
// A session's virtual pads come through SDL's events like real ones (only virtual ones are taken in a session), so
// its plug and unplug commands exercise the same ports.
#include "device_settings.h"
#include "devices.h"
#include "haptics.h"
#include "response.h"
#include "settings.h"

#include <SDL3/SDL_gamepad.h>

#include <string>
#include <vector>

namespace gt4 {

class PadDevices {
public:
    // `profilesPath`: input_profiles.ini ("" for none: every device has its type's defaults, and nothing is written).
    // `virtualOnly`: a session (only its virtual pads are taken).
    PadDevices(std::string profilesPath, bool virtualOnly);
    ~PadDevices();
    PadDevices(const PadDevices&) = delete;
    PadDevices& operator=(const PadDevices&) = delete;

    // SDL_EVENT_GAMEPAD_ADDED / REMOVED.
    void added(SDL_JoystickID id);
    void removed(SDL_JoystickID id);

    // The gamepad holding `port` (null: none).
    SDL_Gamepad* gamepad(int port) const { return port >= 0 && port < input::Ports::kPorts ? port_[port].gamepad : nullptr; }
    // Whether the game sees a controller in `port`: pad 1 always (the keyboard), pad 2 while a gamepad holds it.
    bool connected(int port) const { return port == 0 || gamepad(port) != nullptr; }
    // The port's pad for this frame: its devices' bindings, then its response (input.response; dt in seconds). A
    // wheel GT4 sees as its Driving Force Pro (dfpPort) isn't part of its port's pad: the keyboard alone is, on pad 1.
    ps2::PadInput read(int port, const Settings& settings, float dt);
    // PLAN.md D7: the port whose gamepad is a wheel (a device SDL types as one) with input.wheel.mode = dfp (its own
    // or the global value), the first such; -1 for none. One wheel: LGDEV's index 0.
    int dfpPort(const Settings& global) const;
    // That wheel's input through its profile's bindings, before D12's response (GT4 applies its own), for
    // input::dfpWheel; false without one. Neutral while one of its bindings is being captured.
    bool wheelInput(const Settings& global, ps2::PadInput& out);
    // The port's motors and triggers now: GT4's own rumble (D2's g_rumble: bit 8 the small motor, bits 0-7 the large
    // one) and the physics' haptics (D14), the larger per motor; sent when they change and every second while on
    // (SDL's rumble lasts as long as asked). Triggers: an Xbox pad's impulse triggers, or a DualSense's adaptive ones.
    void feel(int port, uint32_t gt4Rumble, const input::HapticsOut& haptics, uint64_t nowMs);
    // host.log says, per pad: at its open, the driver SDL opened it through, SDL's rumble capabilities and a
    // zero-strength call's result; the first time GT4's rumble and the physics' haptics each turn its motors on, with
    // SDL's result; the first failure; and every 30 s while anything is asked, the strongest asks and sends.
    // The rumble test (the Odin's report, 2026-11-09): every pad's motors in numbered pulses (pad_devices.cpp's
    // kTestSteps: each motor through SDL for 1.5 s, then on Windows both through XInputSetState itself), each result
    // logged with its time. rumbleTest starts it (`why`: what asked, for the log); rumbleTestStep, called every
    // main-loop pass, runs it; feel() leaves the motors to it meanwhile.
    void rumbleTest(uint64_t nowMs, const char* why);
    void rumbleTestStep(uint64_t nowMs);
    // The settings `port` runs with: the globals with its devices' own (device_settings.h, gated by the globals'
    // original). The globals themselves when its devices have none (the usual case: no copy), else the port's copy,
    // valid until the next call for that port.
    const Settings& settingsFor(int port, const Settings& global) const;

    // The settings pages (PLAN.md D13, e86394d; lane M's input.device.<key>.<setting>). All on the host's main thread,
    // where the events and the pads are handled too. A change is saved to input_profiles.ini at once (whole or not at
    // all; nothing when the host runs without the file, as sessions do).
    struct Device {
        std::string key, name;     // the key holds no dot
        input::DeviceType type = input::DeviceType::Gamepad;
        int port = -1;             // the port it holds (-1: not connected); the keyboard: 0
    };
    std::vector<Device> devices() const;
    // A device's own value of input.<setting>, or "" when it takes the global one.
    std::string deviceValue(const std::string& key, const std::string& setting) const;
    // Sets it ("" clears it, back to the global); "" or why not (an unknown device, a setting a device can't have, an
    // invalid value: the option table's message).
    std::string setDeviceValue(const std::string& key, const std::string& setting, const std::string& value);
    // An action's bindings as text (devices.h bindingText), and setting them: "" or why not (a parse error, or
    // checkProfile's refusal: a remap that would leave the menus unusable).
    std::string bindings(const std::string& key, uint32_t action) const;
    std::string setBindings(const std::string& key, uint32_t action, const std::string& text);
    // A device back to its type's defaults: bindings and settings.
    std::string resetDevice(const std::string& key);
    // Capturing a binding: the next key (the keyboard) or button or axis past half travel (a gamepad) of that device
    // becomes the result; until then that device's input does not reach the game. The caller takes the result and
    // decides (setBindings with it, alone or added).
    bool beginCapture(const std::string& key, uint32_t action);
    void cancelCapture();
    struct Captured {
        std::string key;
        uint32_t action = 0;
        input::Binding binding;
    };
    bool takeCapture(Captured& out);

private:
    struct Port {
        SDL_Gamepad* gamepad = nullptr;
        int profile = -1;          // in profiles_ (-1: none)
        input::Map map;            // the keyboard's (pad 1) and the gamepad's bindings
        input::Response response{map};
        mutable Settings settings;   // settingsFor's copy when its devices have their own values
        // what feel() last sent
        uint16_t low = 0, high = 0, triggerLeft = 0, triggerRight = 0;
        uint64_t motorsAt = 0, triggersAt = 0;
        bool noImpulseTriggers = false;   // SDL said no (XInput, Bluetooth): said once, not tried again
        uint8_t ds5Brake = 0, ds5Throttle = 0;   // a DualSense's trigger effects, in 16 steps (0: off)
        bool ds5Sent = false;
        bool gt4Said = false, physicsSaid = false, failSaid = false;   // (host.log's lines, once each)
        // host.log's rumble summary every 30 s while anything was asked: the strongest asks and sends, and the calls
        struct Asked {
            uint64_t since = 0;
            uint32_t frames = 0, smallOn = 0, calls = 0, failed = 0;
            uint8_t large = 0;
            float physicsLow = 0.0f, physicsHigh = 0.0f;
            uint16_t low = 0, high = 0;
        } asked;
        static constexpr int kTestDone = 100;
        int testPhase = kTestDone;   // the rumble test: -1 to start, then its last step taken (kTestSteps), or done
    };
    void take(SDL_Gamepad* pad, int port);
    uint64_t testAt_ = 0;   // the rumble test's start
    int profileFor(SDL_JoystickID id);   // found or added (the type's defaults)
    void rebuild(int port);
    void save() const;

    std::string path_;
    bool virtualOnly_;
    std::vector<input::Profile> profiles_;   // [0] the keyboard
    input::Ports ports_;
    Port port_[input::Ports::kPorts];
    std::vector<SDL_Gamepad*> waiting_;      // opened while both ports were held
    // a capture under way: the device, the action, and the input at its start (what changes from it is captured)
    struct Capture {
        bool on = false, done = false;
        std::string key;
        uint32_t action = 0;
        input::Snapshot start;
        bool started = false;
        input::Binding result;
    } capture_;
    int profileIndex(const std::string& key) const;
    void watchCapture(const input::Snapshot& s, bool keyboard, const std::string& key);
};

// --rumble-test: every gamepad SDL finds (within 2 s) opened, its rumble capabilities logged, and the test's pulses
// run on it (pad_devices.cpp's kTestSteps: each motor through SDL for 1.5 s, then on Windows both through
// XInputSetState itself), each result logged with its time, and closed. SDL's gamepad subsystem must be up; no game,
// no window. Exit status: 0 every SDL pulse ok, 1 a pad failed, 3 no gamepad found (the direct pulse is logged only).
int rumbleTestOnly();

}  // namespace gt4
