#pragma once
// The host's input devices for the game's two pads (PLAN.md D13; platform/input/devices.h): the gamepads open and the
// ports they hold (a device keeps its port across a reconnect), each device's profile from input_profiles.ini (its
// bindings to the PS2 pad), and each port's response (D12). Pad 1 has the keyboard too.
//
// A session's virtual pads come through SDL's events like real ones (only virtual ones are taken in a session), so
// its plug and unplug commands exercise the same ports.
#include "devices.h"
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
    // The port's pad for this frame: its devices' bindings, then its response (input.response; dt in seconds).
    ps2::PadInput read(int port, const Settings& settings, float dt);

private:
    struct Port {
        SDL_Gamepad* gamepad = nullptr;
        int profile = -1;          // in profiles_ (-1: none)
        input::Map map;            // the keyboard's (pad 1) and the gamepad's bindings
        input::Response response{map};
    };
    void take(SDL_Gamepad* pad, int port);
    int profileFor(SDL_JoystickID id);   // found or added (the type's defaults)
    void rebuild(int port);
    void save() const;

    std::string path_;
    bool virtualOnly_;
    std::vector<input::Profile> profiles_;   // [0] the keyboard
    input::Ports ports_;
    Port port_[input::Ports::kPorts];
    std::vector<SDL_Gamepad*> waiting_;      // opened while both ports were held
};

}  // namespace gt4
