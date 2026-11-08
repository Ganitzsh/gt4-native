// Devices: profiles, ports and remap checks (devices.h).
#include "devices.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace gt4::input {

namespace {

// SDL3's enums, in their order (checked at the ends against SDL's values).
const char* const kButtons[] = {"south", "east", "west", "north", "back", "guide", "start", "leftstick", "rightstick",
                                "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1",
                                "rightpaddle1", "leftpaddle1", "rightpaddle2", "leftpaddle2", "touchpad", "misc2",
                                "misc3", "misc4", "misc5", "misc6"};
static_assert(std::size(kButtons) == SDL_GAMEPAD_BUTTON_COUNT);
static_assert(SDL_GAMEPAD_BUTTON_DPAD_RIGHT == 14 && SDL_GAMEPAD_BUTTON_TOUCHPAD == 20);
const char* const kAxes[] = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
static_assert(std::size(kAxes) == SDL_GAMEPAD_AXIS_COUNT && SDL_GAMEPAD_AXIS_RIGHT_TRIGGER == 5);
// ps2::PadButton's order, then the sticks (input_map.h Action)
const char* const kActions[] = {"select", "l3", "r3", "start", "up", "right", "down", "left", "l2", "r2", "l1", "r1",
                                "triangle", "circle", "cross", "square", "leftx", "lefty", "rightx", "righty"};
static_assert(std::size(kActions) == kActionCount);

// Keys by scancode: letters, digits, the usual others; any other key is #N (its scancode).
std::string keyName(int code) {
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) return std::string(1, static_cast<char>('a' + code - SDL_SCANCODE_A));
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9) return std::string(1, static_cast<char>('1' + code - SDL_SCANCODE_1));
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) return "f" + std::to_string(code - SDL_SCANCODE_F1 + 1);
    switch (code) {
    case SDL_SCANCODE_0: return "0";
    case SDL_SCANCODE_RETURN: return "return";
    case SDL_SCANCODE_ESCAPE: return "escape";
    case SDL_SCANCODE_BACKSPACE: return "backspace";
    case SDL_SCANCODE_TAB: return "tab";
    case SDL_SCANCODE_SPACE: return "space";
    case SDL_SCANCODE_UP: return "up";
    case SDL_SCANCODE_DOWN: return "down";
    case SDL_SCANCODE_LEFT: return "left";
    case SDL_SCANCODE_RIGHT: return "right";
    case SDL_SCANCODE_LCTRL: return "lctrl";
    case SDL_SCANCODE_LSHIFT: return "lshift";
    case SDL_SCANCODE_LALT: return "lalt";
    case SDL_SCANCODE_RCTRL: return "rctrl";
    case SDL_SCANCODE_RSHIFT: return "rshift";
    case SDL_SCANCODE_RALT: return "ralt";
    default: return "#" + std::to_string(code);
    }
}

int keyCode(std::string_view name) {
    if (name.size() > 1 && name[0] == '#') {
        const int code = std::atoi(std::string(name.substr(1)).c_str());
        return code > 0 && code < SDL_SCANCODE_COUNT ? code : -1;
    }
    for (int code = 1; code < SDL_SCANCODE_COUNT; ++code)
        if (keyName(code)[0] != '#' && keyName(code) == name) return code;
    return -1;
}

template <size_t N>
int indexOf(const char* const (&names)[N], std::string_view name) {
    for (size_t i = 0; i < N; ++i)
        if (name == names[i]) return static_cast<int>(i);
    return -1;
}

std::string number(float v) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", static_cast<double>(v));
    return buffer;
}

bool sameBinding(const Binding& a, const Binding& b) {
    return a.source == b.source && a.code == b.code && a.scale == b.scale && a.deadZone == b.deadZone;
}

bool sameBindings(const std::vector<Binding>& a, const std::vector<Binding>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), sameBinding);
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

const char* typeName(DeviceType t) { return t == DeviceType::Keyboard ? "keyboard" : t == DeviceType::Wheel ? "wheel" : "gamepad"; }

}  // namespace

const char* actionName(uint32_t action) { return action < kActionCount ? kActions[action] : "?"; }

Profile defaultProfile(DeviceType type, std::string key, std::string name) {
    Profile p;
    p.key = std::move(key);
    p.name = std::move(name);
    p.type = type;
    const Map original = originalMap();
    p.map.name = typeName(type);
    for (uint32_t a = 0; a < kActionCount; ++a)
        for (const Binding& b : original.bindings[a])
            if ((b.source == Source::Key) == (type == DeviceType::Keyboard)) p.map.bindings[a].push_back(b);
    return p;
}

Map padMap(const Profile* keyboard, const Profile* gamepad) {
    Map m;
    m.name = "pad";
    for (uint32_t a = 0; a < kActionCount; ++a) {
        if (keyboard) m.bindings[a] = keyboard->map.bindings[a];
        if (gamepad) m.bindings[a].insert(m.bindings[a].end(), gamepad->map.bindings[a].begin(), gamepad->map.bindings[a].end());
    }
    return m;
}

Check checkProfile(const Profile& profile) {
    using namespace ps2;
    Check c;
    const PadButton menu[] = {PadCross, PadCircle, PadTriangle, PadStart, PadUp, PadDown, PadLeft, PadRight};
    for (PadButton b : menu)
        if (profile.map.bindings[b].empty())
            c.refused.push_back(std::string(actionName(b)) + " is unbound: the menus need it");
    for (uint32_t a = 0; a < PadButtonCount; ++a) {
        const bool analogue = std::any_of(profile.map.bindings[a].begin(), profile.map.bindings[a].end(),
                                          [](const Binding& b) { return b.source == Source::Axis; });
        if (!analogue) continue;
        if (a == PadCross || a == PadCircle)
            c.refused.push_back(std::string("an analogue input on ") + actionName(a) +
                                ": a slight pull would confirm or cancel in the menus");
        else if (a != PadL2 && a != PadR2)
            c.warnings.push_back(std::string("an analogue input on ") + actionName(a) +
                                 ": GT4 reads it in menus, where a slight pull presses it");
    }
    return c;
}

std::string bindingText(const std::vector<Binding>& bindings) {
    std::string out;
    for (const Binding& b : bindings) {
        if (!out.empty()) out += ' ';
        switch (b.source) {
        case Source::Key: out += "key:" + keyName(b.code); break;
        case Source::Button: out += std::string("button:") + (b.code >= 0 && b.code < SDL_GAMEPAD_BUTTON_COUNT ? kButtons[b.code] : "?"); break;
        case Source::Axis: out += std::string("axis:") + (b.code >= 0 && b.code < SDL_GAMEPAD_AXIS_COUNT ? kAxes[b.code] : "?"); break;
        }
        if (b.scale != 1.0f) out += ":" + number(b.scale);
        if (b.deadZone != 0.0f) out += "@" + number(b.deadZone);
    }
    return out;
}

bool parseBindings(std::string_view text, std::vector<Binding>& out, std::string* error) {
    out.clear();
    const auto fail = [&](const std::string& why) {
        if (error) *error = why;
        out.clear();
        return false;
    };
    text = trim(text);
    while (!text.empty()) {
        const size_t space = text.find_first_of(" \t");
        std::string_view token = text.substr(0, space);
        text = space == std::string_view::npos ? std::string_view() : trim(text.substr(space));
        Binding b;
        if (const size_t at = token.find('@'); at != std::string_view::npos) {
            char* end = nullptr;
            const std::string zone(token.substr(at + 1));
            b.deadZone = std::strtof(zone.c_str(), &end);
            if (zone.empty() || *end || b.deadZone < 0.0f || b.deadZone >= 1.0f) return fail("bad dead zone in '" + std::string(token) + "'");
            token = token.substr(0, at);
        }
        const size_t c1 = token.find(':');
        if (c1 == std::string_view::npos) return fail("expected key:, button: or axis: in '" + std::string(token) + "'");
        const std::string_view source = token.substr(0, c1);
        std::string_view name = token.substr(c1 + 1);
        if (const size_t c2 = name.find(':'); c2 != std::string_view::npos) {
            char* end = nullptr;
            const std::string scale(name.substr(c2 + 1));
            b.scale = std::strtof(scale.c_str(), &end);
            if (scale.empty() || *end || b.scale == 0.0f) return fail("bad scale in '" + std::string(token) + "'");
            name = name.substr(0, c2);
        }
        if (source == "key") {
            b.source = Source::Key;
            b.code = keyCode(name);
        } else if (source == "button") {
            b.source = Source::Button;
            b.code = indexOf(kButtons, name);
        } else if (source == "axis") {
            b.source = Source::Axis;
            b.code = indexOf(kAxes, name);
        } else {
            return fail("unknown source '" + std::string(source) + "'");
        }
        if (b.code < 0) return fail("unknown " + std::string(source) + " '" + std::string(name) + "'");
        out.push_back(b);
    }
    return true;
}

std::string formatProfiles(const std::vector<Profile>& profiles) {
    std::string out =
        "# GT4 native host: input devices (docs/modern/PLAN.md D13). One section per device: [keyboard], or\n"
        "# [gamepad GUID] / [wheel GUID]. An action's line replaces the device's default bindings for it (the PS2 pad's\n"
        "# places); `action =` with nothing unbinds it. Bindings: key:NAME, button:NAME, axis:NAME[:SCALE][@DEADZONE].\n"
        "# GT4's own controller screen assigns accelerate, brake and the rest to the PS2 pad's buttons.\n";
    for (const Profile& p : profiles) {
        out += "\n[" + std::string(typeName(p.type)) + (p.type == DeviceType::Keyboard ? "" : " " + p.key) + "]\n";
        if (!p.name.empty()) out += "name = " + p.name + "\n";
        const Profile defaults = defaultProfile(p.type, p.key, p.name);
        for (uint32_t a = 0; a < kActionCount; ++a)
            if (!sameBindings(p.map.bindings[a], defaults.map.bindings[a]))
                out += std::string(actionName(a)) + " = " + bindingText(p.map.bindings[a]) + "\n";
    }
    return out;
}

std::vector<Profile> parseProfiles(std::string_view text, std::vector<std::string>* warnings) {
    std::vector<Profile> profiles;
    Profile* current = nullptr;
    int number = 0;
    const auto warn = [&](const std::string& what) {
        if (warnings) warnings->push_back("line " + std::to_string(number) + ": " + what);
    };
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        std::string_view line = trim(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        ++number;
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[') {
            current = nullptr;
            if (line.back() != ']') { warn("malformed section"); continue; }
            const std::string_view inside = trim(line.substr(1, line.size() - 2));
            const size_t space = inside.find(' ');
            const std::string_view kind = inside.substr(0, space);
            const std::string key = space == std::string_view::npos ? std::string() : std::string(trim(inside.substr(space)));
            DeviceType type;
            if (kind == "keyboard" && key.empty()) type = DeviceType::Keyboard;
            else if (kind == "gamepad" && !key.empty()) type = DeviceType::Gamepad;
            else if (kind == "wheel" && !key.empty()) type = DeviceType::Wheel;
            else { warn("unknown section [" + std::string(inside) + "]"); continue; }
            profiles.push_back(defaultProfile(type, type == DeviceType::Keyboard ? "keyboard" : key, ""));
            current = &profiles.back();
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) { warn("expected NAME = VALUE"); continue; }
        if (!current) { warn("a line outside a device's section"); continue; }
        const std::string_view name = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (name == "name") { current->name = std::string(value); continue; }
        const int action = indexOf(kActions, name);
        if (action < 0) { warn("unknown action '" + std::string(name) + "'"); continue; }
        std::vector<Binding> bindings;
        std::string error;
        if (!parseBindings(value, bindings, &error)) { warn(std::string(name) + ": " + error); continue; }
        const bool keyboard = current->type == DeviceType::Keyboard;
        if (std::any_of(bindings.begin(), bindings.end(), [&](const Binding& b) { return (b.source == Source::Key) != keyboard; })) {
            warn(std::string(name) + ": " + (keyboard ? "only keys bind on the keyboard" : "keys bind on the keyboard's section"));
            continue;
        }
        current->map.bindings[action] = std::move(bindings);
    }
    return profiles;
}

int Ports::connect(uint32_t id, const std::string& key) {
    if (const int p = portOf(id); p >= 0) return p;
    int port = -1;
    for (int p = 0; p < kPorts && port < 0; ++p)   // the port this key last held, when free
        if (!held_[p].id && last_[p] == key) port = p;
    for (int p = 0; p < kPorts && port < 0; ++p)   // a free port no absent device last held
        if (!held_[p].id && last_[p].empty()) port = p;
    for (int p = 0; p < kPorts && port < 0; ++p)   // any free port
        if (!held_[p].id) port = p;
    if (port >= 0) held_[port] = {id, key};
    return port;
}

int Ports::disconnect(uint32_t id) {
    const int p = portOf(id);
    if (p < 0) return -1;
    last_[p] = held_[p].key;
    held_[p] = {};
    return p;
}

int Ports::portOf(uint32_t id) const {
    for (int p = 0; p < kPorts; ++p)
        if (id && held_[p].id == id) return p;
    return -1;
}

}  // namespace gt4::input
