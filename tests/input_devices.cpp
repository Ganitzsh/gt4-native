// platform/input/devices.cpp (PLAN.md D13): the default profiles give the `original` map exactly (no profile file:
// nothing changes); profiles round-trip through input_profiles.ini's text; each refused remap is refused (a menu
// input unbound, an analogue input on Cross or Circle) and an analogue input on another menu button warns; ports
// stay with their devices across reconnects (by GUID, then by order), including two pads of one model.
#include "devices.h"

#include <cstdio>
#include <cstring>
#include <random>

using namespace gt4;
using namespace gt4::input;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

bool sameMap(const Map& a, const Map& b) {
    for (uint32_t k = 0; k < kActionCount; ++k) {
        if (a.bindings[k].size() != b.bindings[k].size()) return false;
        for (size_t i = 0; i < a.bindings[k].size(); ++i) {
            const Binding &x = a.bindings[k][i], &y = b.bindings[k][i];
            if (x.source != y.source || x.code != y.code || x.scale != y.scale || x.deadZone != y.deadZone ||
                x.full != y.full)
                return false;
        }
    }
    return true;
}

bool contains(const std::vector<std::string>& lines, const char* text) {
    for (const auto& l : lines)
        if (l.find(text) != std::string::npos) return true;
    return false;
}

}  // namespace

int main() {
    const Profile keyboard = defaultProfile(DeviceType::Keyboard, "keyboard", "Keyboard");
    const Profile pad = defaultProfile(DeviceType::Gamepad, "03000000de280000ff11000001000000", "Pad");

    // no profile file: the defaults are the original map, binding for binding, and so byte for byte
    const Map original = originalMap();
    expect(sameMap(padMap(&keyboard, &pad), original), "keyboard + gamepad defaults = the original map");
    {
        std::mt19937 random(7);
        bool same = true;
        const Map combined = padMap(&keyboard, &pad);
        for (int i = 0; i < 5000; ++i) {
            Snapshot s;
            s.gamepad = (random() & 3) != 0;
            for (bool& k : s.keys) k = (random() % 23) == 0;
            for (bool& b : s.buttons) b = (random() & 3) == 0;
            for (float& a : s.axes) a = std::uniform_real_distribution<float>(-1.00003f, 1.0f)(random);
            uint8_t want[18], got[18];
            ps2::encodePad(apply(original, s), want);
            ps2::encodePad(apply(combined, s), got);
            same &= std::memcmp(want, got, 18) == 0;
        }
        expect(same, "the defaults' bytes for 5000 random inputs");
    }
    expect(sameMap(padMap(nullptr, &pad), padMap(nullptr, &pad)) && padMap(nullptr, &pad).bindings[kLeftX].size() == 1,
           "pad 2: its gamepad only");

    // defaults pass the checks with no warnings (the triggers on L2 and R2 are their own places)
    {
        const Check kc = checkProfile(keyboard), pc = checkProfile(pad);
        expect(kc.ok() && kc.warnings.empty() && pc.ok() && pc.warnings.empty(), "the defaults pass");
    }
    // refused: a menu input unbound; an analogue input on Cross or Circle. Warned: analogue on another menu button.
    {
        Profile p = pad;
        p.map.bindings[ps2::PadTriangle].clear();
        expect(!checkProfile(p).ok() && contains(checkProfile(p).refused, "triangle"), "triangle unbound: refused");
        p = pad;
        p.map.bindings[ps2::PadUp].clear();
        expect(!checkProfile(p).ok(), "d-pad up unbound: refused");
        p = keyboard;
        p.map.bindings[ps2::PadStart].clear();
        expect(!checkProfile(p).ok(), "start unbound on the keyboard: refused");
        p = pad;
        p.map.bindings[ps2::PadCross] = {{Source::Axis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f, 0.0f}};
        expect(!checkProfile(p).ok() && contains(checkProfile(p).refused, "cross"), "a trigger on cross: refused");
        p = pad;
        p.map.bindings[ps2::PadCircle].push_back({Source::Axis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f, 0.0f});
        expect(!checkProfile(p).ok(), "a trigger added to circle: refused");
        p = pad;
        p.map.bindings[ps2::PadSquare] = {{Source::Axis, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f, 0.0f}};
        const Check c = checkProfile(p);
        expect(c.ok() && contains(c.warnings, "square"), "a trigger on square: a warning");
        p = pad;   // the PS2 places swapped among themselves is the player's own choice
        std::swap(p.map.bindings[ps2::PadCross], p.map.bindings[ps2::PadCircle]);
        expect(checkProfile(p).ok(), "cross and circle swapped: allowed");
    }

    // round trip: defaults write no action lines; changes, scales, dead zones and an unbound action come back
    {
        expect(formatProfiles({keyboard, pad}).find(" = key:") == std::string::npos &&
                   formatProfiles({keyboard, pad}).find(" = button:") == std::string::npos,
               "defaults: no action lines");
        Profile k = keyboard, g = pad;
        k.map.bindings[kLeftX] = {{Source::Key, SDL_SCANCODE_A, -1.0f, 0.0f}, {Source::Key, SDL_SCANCODE_D, 1.0f, 0.0f}};
        k.map.bindings[ps2::PadCross] = {{Source::Key, SDL_SCANCODE_SPACE, 1.0f, 0.0f}, {Source::Key, 200, 1.0f, 0.0f}};
        g.map.bindings[ps2::PadR1] = {};
        g.map.bindings[kRightX] = {{Source::Axis, SDL_GAMEPAD_AXIS_RIGHTX, -1.0f, 0.12f}};
        g.map.bindings[ps2::PadL1] = {{Source::Button, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, 1.0f, 0.0f}};
        const std::string text = formatProfiles({k, g});
        std::vector<std::string> warnings;
        const std::vector<Profile> back = parseProfiles(text, &warnings);
        expect(warnings.empty(), "round trip: no warnings");
        expect(back.size() == 2 && back[0].type == DeviceType::Keyboard && back[0].key == "keyboard" &&
                   back[0].name == "Keyboard" && back[1].type == DeviceType::Gamepad && back[1].key == g.key &&
                   back[1].name == "Pad",
               "round trip: the devices, keys and names");
        expect(back.size() == 2 && sameMap(back[0].map, k.map) && sameMap(back[1].map, g.map), "round trip: the maps");
        expect(text.find("rightx = axis:rightx:-1@0.12") != std::string::npos && text.find("r1 = \n") != std::string::npos &&
                   text.find("cross = key:space key:#200") != std::string::npos,
               "the file's spelling");
    }
    // a bad file: warnings, never fatal; the good lines still count
    {
        std::vector<std::string> warnings;
        const auto p = parseProfiles("[gamepad abc]\nname = X\nsquare = button:north\nwarp = button:south\n"
                                     "cross = key:x\ncircle = axis:nowhere\n[mouse]\ntriangle = button:south\n[keyboard\n",
                                     &warnings);
        expect(p.size() == 1 && p[0].map.bindings[ps2::PadSquare].size() == 1 &&
                   p[0].map.bindings[ps2::PadSquare][0].code == SDL_GAMEPAD_BUTTON_NORTH,
               "a bad file: the good line taken");
        expect(p.size() == 1 && p[0].map.bindings[ps2::PadCross].size() == 1 &&
                   p[0].map.bindings[ps2::PadCross][0].source == Source::Button,
               "a bad file: a key in a gamepad's section left at the default");
        expect(warnings.size() == 6, "a bad file: six warnings");
    }

    // ports: by GUID, then by order
    {
        Ports ports;
        expect(ports.connect(11, "xbox") == 0 && ports.connect(12, "dualsense") == 1, "two pads: ports 1 and 2");
        expect(ports.connect(13, "other") == -1, "a third waits");
        expect(ports.disconnect(11) == 0 && ports.holder(0) == 0, "the first leaves: port 1 empty");
        expect(ports.connect(21, "xbox") == 0, "it comes back (a new id): its port");
        expect(ports.disconnect(12) == 1 && ports.disconnect(21) == 0, "both leave");
        expect(ports.connect(31, "dualsense") == 1, "the second comes back first: still its port");
        expect(ports.connect(32, "new") == 0, "a new pad: the free port");
        expect(ports.connect(33, "xbox") == -1, "the first pad finds both held");
        Ports twins;
        expect(twins.connect(1, "ds") == 0 && twins.connect(2, "ds") == 1, "two pads of one model: two ports");
        expect(twins.disconnect(1) == 0 && twins.connect(5, "ds") == 0 && twins.portOf(2) == 1,
               "one of the twins reconnects: the free port of the two");
        expect(twins.connect(5, "ds") == 0, "connect is idempotent");
    }

    // PLAN.md D7's pedals: an axis over its whole travel (:full), resting at either end, and one axis split in two
    {
        std::vector<Binding> b;
        std::string error;
        expect(parseBindings("axis:lefty:-1:full@0.05 axis:righty:full", b, &error) && b.size() == 2 && b[0].full &&
                   b[0].scale == -1.0f && b[0].deadZone == 0.05f && b[1].full && b[1].scale == 1.0f,
               ":full parsed, with a scale and a dead zone");
        expect(bindingText(b) == "axis:lefty:-1:full@0.05 axis:righty:full", ":full written back as read");
        expect(!parseBindings("button:south:full", b, &error), ":full only for an axis");
        Map m;
        parseBindings("axis:lefty:-1:full", m.bindings[ps2::PadR2]);   // a pedal resting at the top (+1)
        parseBindings("axis:righty:-1", m.bindings[ps2::PadL2]);       // a combined axis: brake on its negative half
        parseBindings("axis:righty", m.bindings[ps2::PadL1]);          // and the other pedal on its positive half
        Snapshot s;
        s.gamepad = true;
        auto at = [&](float lefty, float righty) {
            s.axes[SDL_GAMEPAD_AXIS_LEFTY] = lefty;
            s.axes[SDL_GAMEPAD_AXIS_RIGHTY] = righty;
            return apply(m, s);
        };
        expect(at(1.0f, 0.0f).button[ps2::PadR2] == 0.0f, ":full, scale -1: at rest (+1) reads 0");
        expect(at(-1.0f, 0.0f).button[ps2::PadR2] == 1.0f, ":full, scale -1: fully pressed (-1) reads 1");
        expect(at(0.0f, 0.0f).button[ps2::PadR2] == 0.5f, ":full: half travel reads half");
        const ps2::PadInput split = at(1.0f, -0.6f);
        expect(split.button[ps2::PadL2] > 0.59f && split.button[ps2::PadL1] == 0.0f, "a combined axis: one half each");
    }

    if (failures) return 1;
    std::printf("input_devices: all checks passed\n");
    return 0;
}
