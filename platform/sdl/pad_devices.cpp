// The host's input devices for the game's pads (pad_devices.h).
#include "pad_devices.h"

#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace gt4 {

namespace {

void calm(SDL_Gamepad* pad, bool ds5Sent);   // (below: before a pad is closed)

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

PadDevices::~PadDevices() {   // (before SDL_Quit: main's SdlRuntime outlives it)
    for (auto& p : port_)
        if (p.gamepad) {
            calm(p.gamepad, p.ds5Sent);
            SDL_CloseGamepad(p.gamepad);
        }
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
    calm(port_[port].gamepad, port_[port].ds5Sent);
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
    Port& p = port_[port];
    p.low = p.high = p.triggerLeft = p.triggerRight = 0;   // (a new pad: nothing sent to it yet)
    p.motorsAt = p.triggersAt = 0;
    p.noImpulseTriggers = false;
    p.ds5Brake = p.ds5Throttle = 0;
    p.ds5Sent = false;
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
    input::Snapshot s = readInput(p.gamepad);
    // a binding being captured: that device's input is watched, and kept from the game until it is done
    if (capture_.on && !capture_.done) {
        if (port == 0 && capture_.key == profiles_[0].key) {
            watchCapture(s, true, capture_.key);
            std::fill(std::begin(s.keys), std::end(s.keys), false);
        } else if (p.profile >= 0 && profiles_[p.profile].key == capture_.key) {
            watchCapture(s, false, capture_.key);
            s.gamepad = false;
        }
    }
    ps2::PadInput in = input::apply(p.map, s);
    p.response.step(in, s, responseConfig(settingsFor(port, settings)), dt);
    return in;
}

const Settings& PadDevices::settingsFor(int port, const Settings& global) const {
    const Port& p = port_[port];
    const input::Profile* keyboard = port == 0 ? &profiles_[0] : nullptr;
    const input::Profile* gamepad = p.profile >= 0 ? &profiles_[p.profile] : nullptr;
    if ((!keyboard || keyboard->overrides.empty()) && (!gamepad || gamepad->overrides.empty())) return global;
    p.settings = deviceSettings(global, keyboard, gamepad);
    return p.settings;
}

int PadDevices::profileIndex(const std::string& key) const {
    for (size_t i = 0; i < profiles_.size(); ++i)
        if (profiles_[i].key == key) return static_cast<int>(i);
    return -1;
}

std::vector<PadDevices::Device> PadDevices::devices() const {
    std::vector<Device> out;
    for (size_t i = 0; i < profiles_.size(); ++i) {
        Device d;
        d.key = profiles_[i].key;
        d.name = profiles_[i].name;
        d.type = profiles_[i].type;
        if (i == 0) d.port = 0;
        for (int p = 0; p < input::Ports::kPorts && i != 0; ++p)
            if (port_[p].gamepad && port_[p].profile == static_cast<int>(i)) d.port = p;
        out.push_back(d);
    }
    return out;
}

std::string PadDevices::deviceValue(const std::string& key, const std::string& setting) const {
    const int i = profileIndex(key);
    if (i < 0) return {};
    for (const auto& [name, value] : profiles_[i].overrides)
        if (name == setting) return value;
    return {};
}

std::string PadDevices::setDeviceValue(const std::string& key, const std::string& setting, const std::string& value) {
    const int i = profileIndex(key);
    if (i < 0) return "no device " + key;
    auto& o = profiles_[i].overrides;
    auto at = std::find_if(o.begin(), o.end(), [&](const auto& e) { return e.first == setting; });
    if (value.empty()) {   // cleared: the global value again
        if (!deviceSetting(setting)) return "not a setting a device can have: input." + setting;
        if (at != o.end()) o.erase(at);
    } else {
        if (std::string error = checkDeviceSetting(setting, value); !error.empty()) return error;
        if (at != o.end()) at->second = value;
        else o.emplace_back(setting, value);
    }
    save();
    return {};
}

std::string PadDevices::bindings(const std::string& key, uint32_t action) const {
    const int i = profileIndex(key);
    return i < 0 || action >= input::kActionCount ? std::string() : input::bindingText(profiles_[i].map.bindings[action]);
}

std::string PadDevices::setBindings(const std::string& key, uint32_t action, const std::string& text) {
    const int i = profileIndex(key);
    if (i < 0) return "no device " + key;
    if (action >= input::kActionCount) return "no such action";
    std::vector<input::Binding> list;
    std::string error;
    if (!input::parseBindings(text, list, &error)) return error;
    const bool keyboard = profiles_[i].type == input::DeviceType::Keyboard;
    for (const input::Binding& b : list)
        if ((b.source == input::Source::Key) != keyboard) return keyboard ? "only keys bind on the keyboard" : "keys bind on the keyboard";
    input::Profile candidate = profiles_[i];
    candidate.map.bindings[action] = list;
    const input::Check check = input::checkProfile(candidate);
    if (!check.ok()) {
        std::string why;
        for (const auto& r : check.refused) why += (why.empty() ? "" : "; ") + r;
        return why;
    }
    profiles_[i] = std::move(candidate);
    for (int p = 0; p < input::Ports::kPorts; ++p) rebuild(p);
    save();
    return {};
}

std::string PadDevices::resetDevice(const std::string& key) {
    const int i = profileIndex(key);
    if (i < 0) return "no device " + key;
    const input::Profile& old = profiles_[i];
    profiles_[i] = input::defaultProfile(old.type, old.key, old.name);
    for (int p = 0; p < input::Ports::kPorts; ++p) rebuild(p);
    save();
    return {};
}

bool PadDevices::beginCapture(const std::string& key, uint32_t action) {
    if (profileIndex(key) < 0 || action >= input::kActionCount) return false;
    capture_ = Capture{};
    capture_.on = true;
    capture_.key = key;
    capture_.action = action;
    return true;
}

void PadDevices::cancelCapture() { capture_ = Capture{}; }

bool PadDevices::takeCapture(Captured& out) {
    if (!capture_.on || !capture_.done) return false;
    out.key = capture_.key;
    out.action = capture_.action;
    out.binding = capture_.result;
    capture_ = Capture{};
    return true;
}

// What changed on the captured device since the capture began: a key pressed (the keyboard), or a button pressed or an
// axis moved past half its travel (a gamepad; on a stick action its direction is the binding's sign).
void PadDevices::watchCapture(const input::Snapshot& s, bool keyboard, const std::string&) {
    if (!capture_.started) {
        capture_.start = s;
        capture_.started = true;
        return;
    }
    input::Binding b;
    if (keyboard) {
        for (int k = 0; k < SDL_SCANCODE_COUNT; ++k)
            if (s.keys[k] && !capture_.start.keys[k]) {
                b = {input::Source::Key, k, 1.0f, 0.0f};
                capture_.result = b;
                capture_.done = true;
                return;
            }
        return;
    }
    if (!s.gamepad) return;
    for (int k = 0; k < SDL_GAMEPAD_BUTTON_COUNT; ++k)
        if (s.buttons[k] && !capture_.start.buttons[k]) {
            capture_.result = {input::Source::Button, k, 1.0f, 0.0f};
            capture_.done = true;
            return;
        }
    const bool stick = capture_.action >= input::kLeftX;
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
        const float v = s.axes[a];
        if (std::fabs(v) > 0.5f && std::fabs(v - capture_.start.axes[a]) > 0.5f) {
            capture_.result = {input::Source::Axis, a, stick && v < 0.0f ? -1.0f : 1.0f, stick ? 0.08f : 0.0f};
            capture_.done = true;
            return;
        }
    }
}

namespace {

uint16_t to16(float x) { return static_cast<uint16_t>(std::lround(std::clamp(x, 0.0f, 1.0f) * 65535.0f)); }

// A DualSense's output report effects block (SDL's HIDAPI PS5 driver passes it through SDL_SendGamepadEffect; the layout
// SDL's testcontroller uses). Bits 0x04 / 0x08 of the first byte say the right / left trigger effects are set; the
// rumble and lights bits stay clear, so those are left as they are.
struct Ds5Effects {
    uint8_t enable1, enable2, rumbleRight, rumbleLeft, headphoneVolume, speakerVolume, micVolume, audioEnable;
    uint8_t micLight, audioMute;
    uint8_t rightTrigger[11], leftTrigger[11];
    uint8_t unknown1[6], ledFlags, unknown2[2], ledAnim, ledBrightness, padLights, red, green, blue;
};
static_assert(sizeof(Ds5Effects) == 47);

// Trigger effects (testcontroller's): 0x05 off; 0x01 resistance from a start position with a force; 0x06 vibration
// at a frequency and an amplitude (0..63) from a start position.
void ds5Resistance(uint8_t out[11], uint8_t step) {   // step 0..15
    std::memset(out, 0, 11);
    out[0] = step ? 0x01 : 0x05;
    if (step) out[2] = static_cast<uint8_t>(40 + step * 14);   // force: firm from the first step, 250 at the last
}
void ds5Vibration(uint8_t out[11], uint8_t step) {
    std::memset(out, 0, 11);
    out[0] = step ? 0x06 : 0x05;
    if (step) {
        out[1] = 15;                                         // frequency
        out[2] = static_cast<uint8_t>(10 + step * 53 / 15);  // amplitude, up to 63
        out[3] = 0;                                          // from the start of the travel
    }
}

// Before a pad is closed (it left its port, or the host ends): its motors stopped, and a DualSense's trigger effects set
// off. Those stay on the pad until something replaces them (SDL's close does not), so a host that quit mid-brake would
// leave the brake stiff in the next program (lane C's review).
void calm(SDL_Gamepad* pad, bool ds5Sent) {
    if (!pad) return;
    SDL_RumbleGamepad(pad, 0, 0, 0);
    if (ds5Sent) {
        Ds5Effects e{};
        e.enable1 = 0x04 | 0x08;
        ds5Resistance(e.leftTrigger, 0);
        ds5Vibration(e.rightTrigger, 0);
        SDL_SendGamepadEffect(pad, &e, sizeof e);
    }
}

}  // namespace

void PadDevices::feel(int port, uint32_t gt4Rumble, const input::HapticsOut& h, uint64_t now) {
    Port& p = port_[port];
    SDL_Gamepad* pad = p.gamepad;
    if (!pad) return;
    // the motors: GT4's (the DUALSHOCK 2's large motor as the low-frequency one, its on / off small one as the high
    // one) and the physics', the larger of each
    const uint16_t low = std::max(static_cast<uint16_t>((gt4Rumble & 0xFF) * 257), to16(h.low));
    const uint16_t high = std::max(static_cast<uint16_t>((gt4Rumble & 0x100) ? 0xFFFF : 0), to16(h.high));
    const bool on = low || high;
    if (low != p.low || high != p.high || (on && now - p.motorsAt >= 1000)) {
        SDL_RumbleGamepad(pad, low, high, on ? 2000 : 0);
        p.low = low;
        p.high = high;
        p.motorsAt = now;
    }
    if (SDL_GetGamepadType(pad) == SDL_GAMEPAD_TYPE_PS5) {
        // adaptive triggers, in 16 steps so a small change sends nothing: the brake's resistance, the throttle's pulse
        const auto step = [](float x) { return static_cast<uint8_t>(std::lround(std::clamp(x, 0.0f, 1.0f) * 15.0f)); };
        const uint8_t brake = step(h.brakeResistance), throttle = step(h.throttlePulse);
        if (p.ds5Sent && brake == p.ds5Brake && throttle == p.ds5Throttle) return;
        Ds5Effects e{};
        e.enable1 = 0x04 | 0x08;
        ds5Vibration(e.rightTrigger, throttle);
        ds5Resistance(e.leftTrigger, brake);
        if (SDL_SendGamepadEffect(pad, &e, sizeof e)) {
            p.ds5Brake = brake;
            p.ds5Throttle = throttle;
            p.ds5Sent = true;
        }
        return;
    }
    // impulse triggers (Xbox, through SDL's Windows.Gaming.Input or GameInput, over USB or the wireless adapter): SDL
    // says false otherwise, which is said once, and then they are not tried again for this pad
    if (p.noImpulseTriggers) return;
    const uint16_t tl = to16(h.triggerLeft), tr = to16(h.triggerRight);
    const bool triggersOn = tl || tr;
    if (tl == p.triggerLeft && tr == p.triggerRight && !(triggersOn && now - p.triggersAt >= 1000)) return;
    if (!SDL_RumbleGamepadTriggers(pad, tl, tr, triggersOn ? 2000 : 0) && triggersOn) {
        p.noImpulseTriggers = true;
        std::printf("Input: port %d: %s: impulse triggers not supported on this connection\n", port + 1,
                    SDL_GetGamepadName(pad));
    }
    p.triggerLeft = tl;
    p.triggerRight = tr;
    p.triggersAt = now;
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
