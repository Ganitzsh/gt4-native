// Input: input.* (lane D; docs/modern/PLAN.md D1, E8; docs/modern/spikes/D1-bindings.md) (settings_table.h).
// The pad map itself is the PS2 mapping (platform/input/input_map.h): GT4's own controller screen assigns functions to
// the pad's buttons, and the host doesn't remap them.
#include "settings_table.h"

namespace gt4::settings_table {

void inputOptions(std::vector<Option>& o) {
    o.push_back(enumOption("input.trigger_travel", &Settings::triggerTravel, {"game", "full"},
        "The gamepad's analogue triggers press L2 and R2 with their pressure. To drive on them, set Gas = R2 and "
        "Brake = L2 in GT4's Options > Controllers > DUALSHOCK 2 button config (the game keeps that in its save). "
        "game = the pressure as the trigger's travel, as on the PS2: GT4 gives full accelerate from 18 % of the "
        "travel and full brake from 33 %. full = the pressure of whichever buttons GT4's controller setup has Gas "
        "and Brake on is scaled so that full comes only at the end of the travel."));
}

}  // namespace gt4::settings_table
