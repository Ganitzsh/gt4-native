#pragma once
// Devices (PLAN.md D13): each input device's profile, the ports the devices hold, and the checks a remap must pass.
//
// A profile belongs to one device, keyed by its SDL GUID (a gamepad) or "keyboard", with its name and type: its
// bindings to the PS2 pad (an action map holding only that device's sources), kept in input_profiles.ini beside
// settings.ini. A device without a profile gets its type's defaults, the PS2 places (the `original` map's part for
// that source): a remap never moves GT4's functions, which GT4's own controller screen assigns to the pad's buttons.
//
// A pad's map is the keyboard's (pad 1 only) followed by its gamepad's; with the default profiles it is the
// `original` map, byte for byte (tests/input_devices.cpp).
//
// No SDL calls: names of keys, buttons and axes are this file's own (SDL's headers only), so tests build without SDL.
#include "input_map.h"

#include <string>
#include <string_view>
#include <vector>

namespace gt4::input {

enum class DeviceType : uint8_t { Keyboard, Gamepad, Wheel };

struct Profile {
    std::string key;    // "keyboard", or the gamepad's GUID (SDL_GUIDToString)
    std::string name;   // the device's name when last seen (for the player; not part of the key)
    DeviceType type = DeviceType::Gamepad;
    Map map;            // this device's bindings only (keys for the keyboard; buttons and axes for a gamepad)
};

// The type's defaults: the `original` map's bindings of that source (a wheel: a gamepad's, until D15).
Profile defaultProfile(DeviceType type, std::string key, std::string name);

// Pad `port`'s map from its devices: the keyboard's bindings (when given) then the gamepad's, action by action.
Map padMap(const Profile* keyboard, const Profile* gamepad);

// The checks a remap must pass (D13): refused when a menu input is left unbound (Cross, Circle, Triangle: back in
// the Options screens, Start, the D-pad) or an analogue input sits on Cross or Circle (D1's trap: a trigger pull
// confirms); a warning for an analogue input on another button GT4 reads in menus (L2 and R2 excepted: the triggers'
// own places, for the pedals).
struct Check {
    std::vector<std::string> refused;
    std::vector<std::string> warnings;
    bool ok() const { return refused.empty(); }
};
Check checkProfile(const Profile& profile);

// input_profiles.ini: one section per device, [keyboard] or [gamepad GUID] (or [wheel GUID]), with name = and one
// line per bound action (cross = button:south, leftx = key:j:-1 key:l axis:leftx; see bindingText). Actions with no
// line keep the type's defaults; `action =` with nothing unbinds. Unknown lines are warnings, never fatal.
std::string formatProfiles(const std::vector<Profile>& profiles);
std::vector<Profile> parseProfiles(std::string_view text, std::vector<std::string>* warnings = nullptr);

// One action's bindings as text, space-separated: key:NAME, button:NAME, axis:NAME, each with :SCALE when it is not
// 1 (a key's direction on a stick, an axis' sign); an axis' dead zone as @ZONE when not 0.
std::string bindingText(const std::vector<Binding>& bindings);
bool parseBindings(std::string_view text, std::vector<Binding>& out, std::string* error = nullptr);

// The actions' names in the file (cross, circle, ..., leftx, lefty, rightx, righty).
const char* actionName(uint32_t action);

// The game's ports and the gamepads that hold them (hot-plug, D13). A port is held by a device instance (SDL's id:
// two pads of one model share a GUID). A device keeps its port across a reconnect: the port its key (GUID) last held
// when that is free, then by order (the first free port no absent device last held, else the first free one). A port
// whose device left is empty: the game reads it unplugged (scePad2GetState), as on the PS2, until a device takes it.
class Ports {
public:
    static constexpr int kPorts = 2;
    // Device `id` with key `key` arrived: the port it takes, -1 when both are held (it waits for one to free).
    int connect(uint32_t id, const std::string& key);
    // Device `id` left: its port (or -1) is empty now, and remembered for its key.
    int disconnect(uint32_t id);
    // The device holding `port` (0: none).
    uint32_t holder(int port) const { return held_[port].id; }
    int portOf(uint32_t id) const;

private:
    struct Held {
        uint32_t id = 0;
        std::string key;
    };
    Held held_[kPorts];
    std::string last_[kPorts];   // the key that held each port last (kept after it leaves)
};

}  // namespace gt4::input
