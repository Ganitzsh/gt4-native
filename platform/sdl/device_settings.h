#pragma once
// A device's own input settings (PLAN.md D13, e86394d): a profile (platform/input/devices.h) overrides some input.*
// settings for its device; anything else takes the global value. The pages set them as input.device.<key>.<setting>
// (lane M's records), the file keeps them as <setting> = <value> in the device's section.
//
// The gates: while a global gate is at original (input.response for the steering and pedal response, input.haptics for
// the haptics and trigger effects), every device override under it is ignored, so the original preset stays the PS2
// behaviour whatever a profile holds. A device may still choose original under a modern global. The presets never look
// at device overrides (detectPreset doesn't see them).
#include "devices.h"
#include "settings.h"

#include <string>
#include <string_view>

namespace gt4 {

// Whether a device may override input.<setting>: the response (response, steer.*, pedal.*) and the haptics (haptics,
// haptics.*, trigger_effects). Not steer.game_band or trigger_travel: those are GT4's, one value sent to the game.
bool deviceSetting(std::string_view setting);

// "" when ``value`` is a valid value of input.<setting> for a device (the option table's check), else why not.
std::string checkDeviceSetting(std::string_view setting, std::string_view value);

// The settings a port runs with: the globals, then pad 1's keyboard's overrides (when given), then its gamepad's, each
// gated by the globals' original (above). Overrides that no longer validate are skipped.
Settings deviceSettings(const Settings& global, const input::Profile* keyboard, const input::Profile* gamepad);

}  // namespace gt4
