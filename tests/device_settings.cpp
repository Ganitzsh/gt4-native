// platform/sdl/device_settings.cpp and the profile file's overrides (PLAN.md D13, e86394d): which input settings a
// device may hold, their check by the option table, the order (globals, the keyboard's, the gamepad's), the gates (a
// global original ignores every device value under it; a device may still choose original), invalid values skipped,
// the preset untouched, and the overrides' round trip through input_profiles.ini.
#include "device_settings.h"

#include <cstdio>
#include <string>

using namespace gt4;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

input::Profile pad(std::initializer_list<std::pair<const char*, const char*>> overrides) {
    input::Profile p = input::defaultProfile(input::DeviceType::Gamepad, "0300509d5e040000e002000000006800", "Pad");
    for (const auto& [k, v] : overrides) p.overrides.emplace_back(k, v);
    return p;
}

}  // namespace

int main() {
    // which settings a device may hold
    expect(deviceSetting("steer.deadzone") && deviceSetting("pedal.linearity") && deviceSetting("response") &&
               deviceSetting("haptics") && deviceSetting("haptics.strength") && deviceSetting("trigger_effects"),
           "the response's and the haptics' settings");
    expect(!deviceSetting("steer.game_band") && !deviceSetting("trigger_travel") && !deviceSetting("steer.nothing") &&
               !deviceSetting("render.aa"),
           "not GT4's own (game_band, trigger_travel), not unknown ones");
    expect(checkDeviceSetting("steer.deadzone", "3").empty() && !checkDeviceSetting("steer.deadzone", "50").empty() &&
               !checkDeviceSetting("response", "sideways").empty(),
           "values checked by the option table");

    Settings global;   // the defaults: high, modern, physics
    // the gamepad's own value; the keyboard's for pad 1; the gamepad's last
    {
        const input::Profile kb = [] {
            input::Profile k = input::defaultProfile(input::DeviceType::Keyboard, "keyboard", "Keyboard");
            k.overrides = {{"steer.key_rate", "50"}, {"steer.deadzone", "7"}};
            return k;
        }();
        const input::Profile g = pad({{"steer.deadzone", "3"}, {"haptics.strength", "40"}});
        const Settings s = deviceSettings(global, &kb, &g);
        expect(s.steerDeadzone == 3.0f && s.steerKeyRate == 50.0f && s.hapticsStrength == 40.0f,
               "keyboard then gamepad over the globals");
        expect(deviceSettings(global, &kb, nullptr).steerDeadzone == 7.0f, "pad 1 without a gamepad: the keyboard's");
        expect(s.preset == global.preset, "a device never changes the preset");
    }
    // a device may choose original under a modern global
    {
        const input::Profile g = pad({{"response", "original"}, {"haptics", "original"}});
        const Settings s = deviceSettings(global, nullptr, &g);
        expect(s.inputResponse == InputResponse::Original && s.hapticsMode == HapticsMode::Original,
               "a device's own original under modern");
    }
    // a global original ignores every device value under it: the original preset stays the PS2's
    {
        Settings original = global;
        original.inputResponse = InputResponse::Original;
        original.hapticsMode = HapticsMode::Original;
        const input::Profile g = pad({{"response", "modern"}, {"steer.deadzone", "3"}, {"haptics", "physics"},
                                      {"haptics.strength", "40"}, {"trigger_effects", "off"}});
        const Settings s = deviceSettings(original, nullptr, &g);
        expect(s.inputResponse == InputResponse::Original && s.steerDeadzone == original.steerDeadzone &&
                   s.hapticsMode == HapticsMode::Original && s.hapticsStrength == original.hapticsStrength &&
                   s.triggerEffects == original.triggerEffects,
               "original globals: the device's values ignored");
        Settings mixed = global;
        mixed.hapticsMode = HapticsMode::Original;   // only the haptics' gate shut
        const Settings m = deviceSettings(mixed, nullptr, &g);
        expect(m.steerDeadzone == 3.0f && m.hapticsStrength == mixed.hapticsStrength, "each gate on its own");
    }
    // an invalid value in the file is skipped, the rest taken
    {
        const input::Profile g = pad({{"steer.deadzone", "99"}, {"steer.linearity", "2"}, {"steer.game_band", "10"}});
        const Settings s = deviceSettings(global, nullptr, &g);
        expect(s.steerDeadzone == global.steerDeadzone && s.steerLinearity == 2.0f && s.steerGameBand == global.steerGameBand,
               "out of range and not a device's: skipped");
    }
    // the file: overrides written after the bindings and read back; a device key with a dot refused
    {
        input::Profile g = pad({{"steer.deadzone", "3"}, {"haptics.strength", "40"}, {"response", "original"}});
        const std::string text = input::formatProfiles({g});
        std::vector<std::string> warnings;
        const auto back = input::parseProfiles(text, &warnings);
        expect(warnings.empty() && back.size() == 1 && back[0].overrides == g.overrides, "overrides round-trip");
        const auto dotted = input::parseProfiles("[gamepad a.b]\nsteer.deadzone = 3\n", &warnings);
        expect(dotted.empty() && !warnings.empty(), "a device key with a dot refused");
    }
    if (failures) return 1;
    std::printf("device_settings: all checks passed\n");
    return 0;
}
