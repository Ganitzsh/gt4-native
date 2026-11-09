// Input: input.* (lane D; docs/modern/PLAN.md D1, D12, E8; docs/modern/spikes/D1-bindings.md) (settings_table.h).
// The pad map itself is the PS2 mapping (platform/input/input_map.h): GT4's own controller screen assigns functions to
// the pad's buttons, and the host doesn't remap them. The response (platform/input/response.h) shapes the sticks and
// pedals before the game sees them.
#include "settings_table.h"

namespace gt4::settings_table {

void inputOptions(std::vector<Option>& o) {
    o.push_back(enumOption("input.trigger_travel", &Settings::triggerTravel, {"game", "full"},
        "The gamepad's analogue triggers press L2 and R2 with their pressure. To drive on them, set Gas = R2 and "
        "Brake = L2 in GT4's Options > Controllers > DUALSHOCK 2 button config (the game keeps that in its save). "
        "game = the pressure as the trigger's travel, as on the PS2: GT4 gives full accelerate from 18 % of the "
        "travel and full brake from 33 %. full = the pressure of whichever buttons GT4's controller setup has Gas "
        "and Brake on is scaled so that full comes only at the end of the travel."));
    o.push_back(enumOption("input.response", &Settings::inputResponse, {"original", "modern"},
        "How the sticks and pedals respond. original = as before: the sticks cut at 8 % of their travel and jump "
        "from there, nothing else (the input.steer and input.pedal options are not used). modern = the steering "
        "and the pedals through the dead zones, curve and filters below, the keys' steering ramped, and, in races, GT4's "
        "own centre band compensated (input.steer.game_band): GT4 keeps the wheels straight for the first 29 % of a "
        "side of the stick, and original keeps that, as on the PS2."));
    o.push_back(floatOption("input.steer.deadzone", &Settings::steerDeadzone, 0.0f, 30.0f,
        "Steering (left stick X), percent of travel each side: below it the wheels stay straight, above it the "
        "steering starts from straight (the rest of the travel rescaled, no jump)."));
    o.push_back(floatOption("input.steer.outer_deadzone", &Settings::steerOuterDeadzone, 0.0f, 30.0f,
        "Steering: percent of travel before the end of the stick's travel where full lock is reached."));
    o.push_back(floatOption("input.steer.linearity", &Settings::steerLinearity, 1.0f, 3.0f,
        "Steering: the curve's exponent. 1 = linear; above 1, finer steering near the centre and quicker near "
        "the end."));
    o.push_back(floatOption("input.steer.sensitivity", &Settings::steerSensitivity, 0.5f, 2.0f,
        "Steering: a gain before the limit. Above 1, full lock comes before the end of travel; below 1, it never "
        "comes."));
    o.push_back(floatOption("input.steer.saturation", &Settings::steerSaturation, 50.0f, 100.0f,
        "Steering: the most lock the stick gives, percent of GT4's full lock."));
    o.push_back(floatOption("input.steer.smoothing", &Settings::steerSmoothing, 0.0f, 100.0f,
        "Steering: a filter's time constant in ms (0 = none). It smooths a twitchy stick and adds that much lag."));
    o.push_back(floatOption("input.steer.key_rate", &Settings::steerKeyRate, 0.0f, 500.0f,
        "Steering from the keyboard: ms from straight to full lock while a key is held (back to straight twice as "
        "fast). 0 = at once."));
    o.push_back(floatOption("input.steer.game_band", &Settings::steerGameBand, 0.0f, 40.0f,
        "Steering: GT4's own centre band, percent of a side (the car's steering command stays 0 inside it; measured "
        "29). In modern, while a race is loaded, the game starts the steering at the band's edge, so the stick's first "
        "steps past the dead zone above turn the wheels; menus outside a race read the stick as before. 0 = not "
        "compensated. original never compensates it."));
    o.push_back(floatOption("input.pedal.deadzone", &Settings::pedalDeadzone, 0.0f, 30.0f,
        "The pedals (the analogue triggers on L2 and R2, and the right stick's halves): percent of travel before "
        "they start, from 0. GT4's own pedal curve and input.trigger_travel apply after this."));
    o.push_back(floatOption("input.pedal.outer_deadzone", &Settings::pedalOuterDeadzone, 0.0f, 30.0f,
        "The pedals: percent of travel before the end where they read full."));
    o.push_back(floatOption("input.pedal.linearity", &Settings::pedalLinearity, 1.0f, 3.0f,
        "The pedals: the curve's exponent. 1 = linear; above 1, finer near the start of travel."));
    o.push_back(floatOption("input.pedal.smoothing", &Settings::pedalSmoothing, 0.0f, 100.0f,
        "The pedals: a filter's time constant in ms (0 = none)."));
    o.push_back(enumOption("input.haptics", &Settings::hapticsMode, {"original", "physics"},
        "The gamepad's rumble. original = GT4's own (its Options > Vibration; crashes and such), as on the PS2. "
        "physics = that, plus what the car does: understeer, lock-up and wheelspin on the low-frequency motor, kerbs and "
        "rough ground on the high-frequency one (each motor the larger of the two). It asks the game for its cars' state "
        "each frame while a race runs."));
    o.push_back(floatOption("input.haptics.strength", &Settings::hapticsStrength, 0.0f, 100.0f,
        "How strong the physics' rumble and trigger effects are, percent (GT4's own rumble is not scaled)."));
    o.push_back(boolOption("input.haptics.understeer", &Settings::hapticsUndersteer,
        "With input.haptics = physics: the low-frequency motor when the front tyres slide more than the rear ones."));
    o.push_back(boolOption("input.haptics.lockup", &Settings::hapticsLockUp,
        "With input.haptics = physics: the low-frequency motor and the brake's trigger effects when a wheel locks."));
    o.push_back(boolOption("input.haptics.wheelspin", &Settings::hapticsWheelspin,
        "With input.haptics = physics: the low-frequency motor and the throttle's trigger effects when a wheel spins."));
    o.push_back(boolOption("input.haptics.kerbs", &Settings::hapticsKerbs,
        "With input.haptics = physics: the high-frequency motor on kerbs and rough ground."));
    o.push_back(enumOption("input.wheel.mode", &Settings::wheelMode, {"pad", "dfp"},
        "A steering wheel (a device the system reports as a wheel; a gamepad never is). pad = the wheel works as a "
        "DualShock 2, through its bindings. dfp = GT4 sees a Logitech Driving Force Pro, as on the PS2: the wheel's "
        "steering, pedals and buttons go to GT4 as that wheel's, and GT4 drives its force feedback (its steering "
        "force, centring spring and damper). Only with a wheel connected."));
    o.push_back(floatOption("input.wheel.ffb_strength", &Settings::wheelFfbStrength, 0.0f, 100.0f,
        "With input.wheel.mode = dfp: how strong the wheel's force feedback is, percent. The host also caps every "
        "force at 60 % of the wheel's full strength until wheels have been measured. Only with a wheel connected."));
    o.push_back(boolOption("input.trigger_effects", &Settings::triggerEffects,
        "With input.haptics = physics: an Xbox pad's impulse triggers (lock-up on the left, wheelspin on the right; over "
        "USB or the Xbox Wireless Adapter only) and a DualSense's adaptive triggers (the brake's resistance as the front "
        "wheels near lock-up, a pulse on the throttle as the rear ones spin). Steam Input or DS4Windows running can hold "
        "the pad and swallow them."));
}

}  // namespace gt4::settings_table
