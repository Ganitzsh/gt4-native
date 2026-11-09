// A device's own input settings (device_settings.h).
#include "device_settings.h"

namespace gt4 {

namespace {

bool starts(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// Which gate a setting is under: the response's or the haptics'.
bool responseSetting(std::string_view s) { return s == "response" || starts(s, "steer.") || starts(s, "pedal."); }
bool hapticsSetting(std::string_view s) { return s == "haptics" || starts(s, "haptics.") || s == "trigger_effects"; }
// A wheel's (PLAN.md D7): under no gate, as a wheel's mode is not an enhancement
bool wheelSetting(std::string_view s) { return starts(s, "wheel."); }

}  // namespace

bool deviceSetting(std::string_view setting) {
    if (setting == "steer.game_band") return false;   // GT4's, one value for the game
    return (responseSetting(setting) || hapticsSetting(setting) || wheelSetting(setting)) &&
           !getOption(Settings{}, "input." + std::string(setting)).empty();
}

std::string checkDeviceSetting(std::string_view setting, std::string_view value) {
    if (!deviceSetting(setting)) return "not a setting a device can have: input." + std::string(setting);
    Settings scratch;
    return setOption(scratch, "input." + std::string(setting), value);
}

Settings deviceSettings(const Settings& global, const input::Profile* keyboard, const input::Profile* gamepad) {
    Settings out = global;
    const bool responseOpen = global.inputResponse != InputResponse::Original;
    const bool hapticsOpen = global.hapticsMode != HapticsMode::Original;
    for (const input::Profile* p : {keyboard, gamepad}) {
        if (!p) continue;
        for (const auto& [setting, value] : p->overrides) {
            if (!deviceSetting(setting)) continue;
            if (responseSetting(setting) && !responseOpen) continue;
            if (hapticsSetting(setting) && !hapticsOpen) continue;
            Settings scratch = out;
            if (setOption(scratch, "input." + setting, value).empty()) out = scratch;   // (an invalid one is skipped)
        }
    }
    out.preset = global.preset;   // (setOption may have moved it; a device never changes the preset)
    return out;
}

}  // namespace gt4
