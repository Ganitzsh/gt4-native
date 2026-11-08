// The host's input devices for the game's pads (pad_devices.h).
#include "pad_devices.h"

#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace gt4 {

namespace {

// The keyboard and one gamepad now (standard layout), for the pad map (platform/input/input_map.h).
input::Snapshot readInput(SDL_Gamepad* gamepad) {
    input::Snapshot s;
    int count = 0;
    if (const bool* keys = SDL_GetKeyboardState(&count))
        for (int k = 0; k < count && k < SDL_SCANCODE_COUNT; ++k) s.keys[k] = keys[k];
    if (gamepad) {
        s.gamepad = true;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) s.buttons[b] = SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(b));
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            s.axes[a] = static_cast<float>(SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(a))) / 32767.0f;
    }
    return s;
}

// The response's settings (input.response, input.steer.*, input.pedal.*; the settings give percent).
input::ResponseConfig responseConfig(const Settings& s) {
    input::ResponseConfig c;
    c.modern = s.inputResponse == InputResponse::Modern;
    c.steer = {s.steerDeadzone / 100.0f, s.steerOuterDeadzone / 100.0f, s.steerLinearity, s.steerSensitivity,
               s.steerSaturation / 100.0f};
    c.pedal = {s.pedalDeadzone / 100.0f, s.pedalOuterDeadzone / 100.0f, s.pedalLinearity, 1.0f, 1.0f};
    c.steerSmoothingMs = s.steerSmoothing;
    c.pedalSmoothingMs = s.pedalSmoothing;
    c.keyRateMs = s.steerKeyRate;
    return c;
}

// A profile the menus can't be used with is not taken: its reasons are printed and the device keeps its defaults.
void checked(input::Profile& p) {
    const input::Check c = input::checkProfile(p);
    for (const auto& w : c.warnings) std::printf("Input: %s (%s): %s\n", p.name.c_str(), p.key.c_str(), w.c_str());
    if (c.ok()) return;
    for (const auto& r : c.refused) std::printf("Input: %s (%s): refused: %s\n", p.name.c_str(), p.key.c_str(), r.c_str());
    std::printf("Input: %s keeps its default bindings\n", p.name.c_str());
    p.map = input::defaultProfile(p.type, p.key, p.name).map;
}

}  // namespace

PadDevices::PadDevices(std::string profilesPath, bool virtualOnly) : path_(std::move(profilesPath)), virtualOnly_(virtualOnly) {
    if (!path_.empty()) {
        std::ifstream file(std::filesystem::path(std::u8string(path_.begin(), path_.end())), std::ios::binary);
        if (file) {
            std::ostringstream text;
            text << file.rdbuf();
            std::vector<std::string> warnings;
            profiles_ = input::parseProfiles(text.str(), &warnings);
            for (const auto& w : warnings) std::printf("Input: %s: %s\n", path_.c_str(), w.c_str());
        }
    }
    // the keyboard first
    const auto keyboard = std::find_if(profiles_.begin(), profiles_.end(),
                                       [](const input::Profile& p) { return p.type == input::DeviceType::Keyboard; });
    if (keyboard == profiles_.end()) profiles_.insert(profiles_.begin(), input::defaultProfile(input::DeviceType::Keyboard, "keyboard", "Keyboard"));
    else if (keyboard != profiles_.begin()) std::iter_swap(profiles_.begin(), keyboard);
    if (profiles_[0].name.empty()) profiles_[0].name = "Keyboard";
    for (auto& p : profiles_) checked(p);
    for (int p = 0; p < input::Ports::kPorts; ++p) rebuild(p);
}

PadDevices::~PadDevices() {
    for (auto& p : port_)
        if (p.gamepad) SDL_CloseGamepad(p.gamepad);
    for (SDL_Gamepad* g : waiting_) SDL_CloseGamepad(g);
}

void PadDevices::added(SDL_JoystickID id) {
    if (virtualOnly_ && !SDL_IsJoystickVirtual(id)) return;   // a session: its own pads only
    if (ports_.portOf(id) >= 0) return;
    for (SDL_Gamepad* g : waiting_)
        if (SDL_GetGamepadID(g) == id) return;
    SDL_Gamepad* pad = SDL_OpenGamepad(id);
    if (!pad) return;
    char guid[33] = {};
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof guid);
    const int port = ports_.connect(id, guid);
    if (port < 0) {
        waiting_.push_back(pad);
        std::printf("Input: %s waits: both ports are held\n", SDL_GetGamepadName(pad));
        return;
    }
    take(pad, port);
}

void PadDevices::removed(SDL_JoystickID id) {
    for (auto it = waiting_.begin(); it != waiting_.end(); ++it)
        if (SDL_GetGamepadID(*it) == id) {
            SDL_CloseGamepad(*it);
            waiting_.erase(it);
            return;
        }
    const int port = ports_.disconnect(id);
    if (port < 0) return;
    std::printf("Input: port %d: %s left (the game sees the port empty)\n", port + 1, SDL_GetGamepadName(port_[port].gamepad));
    SDL_CloseGamepad(port_[port].gamepad);
    port_[port].gamepad = nullptr;
    port_[port].profile = -1;
    rebuild(port);
    // a device that waited takes the port
    while (!waiting_.empty()) {
        SDL_Gamepad* next = waiting_.front();
        waiting_.erase(waiting_.begin());
        char guid[33] = {};
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(SDL_GetGamepadID(next)), guid, sizeof guid);
        const int p = ports_.connect(SDL_GetGamepadID(next), guid);
        if (p >= 0) {
            take(next, p);
            break;
        }
        waiting_.insert(waiting_.begin(), next);
        break;
    }
}

void PadDevices::take(SDL_Gamepad* pad, int port) {
    port_[port].gamepad = pad;
    port_[port].profile = profileFor(SDL_GetGamepadID(pad));
    rebuild(port);
    std::printf("Input: port %d: %s (id %u, %s)\n", port + 1, SDL_GetGamepadName(pad),
                static_cast<unsigned>(SDL_GetGamepadID(pad)), profiles_[port_[port].profile].key.c_str());
}

int PadDevices::profileFor(SDL_JoystickID id) {
    char guid[33] = {};
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof guid);
    const char* name = SDL_GetGamepadNameForID(id);
    const std::string shown = name ? name : "gamepad";
    for (size_t i = 1; i < profiles_.size(); ++i)
        if (profiles_[i].key == guid) {
            if (profiles_[i].name != shown) {
                profiles_[i].name = shown;
                save();
            }
            return static_cast<int>(i);
        }
    const bool wheel = SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL;
    profiles_.push_back(input::defaultProfile(wheel ? input::DeviceType::Wheel : input::DeviceType::Gamepad, guid, shown));
    save();
    return static_cast<int>(profiles_.size() - 1);
}

void PadDevices::rebuild(int port) {
    Port& p = port_[port];
    p.map = input::padMap(port == 0 ? &profiles_[0] : nullptr, p.profile >= 0 ? &profiles_[p.profile] : nullptr);
    p.response.reset();
}

ps2::PadInput PadDevices::read(int port, const Settings& settings, float dt) {
    Port& p = port_[port];
    const input::Snapshot s = readInput(p.gamepad);
    ps2::PadInput in = input::apply(p.map, s);
    p.response.step(in, s, responseConfig(settings), dt);
    return in;
}

void PadDevices::save() const {
    if (path_.empty()) return;
    const std::filesystem::path path(std::u8string(path_.begin(), path_.end()));
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return;
        file << input::formatProfiles(profiles_);
        if (!file) return;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) std::printf("Input: could not write %s: %s\n", path_.c_str(), error.message().c_str());
}

}  // namespace gt4
