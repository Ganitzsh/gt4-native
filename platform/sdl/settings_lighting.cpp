// Night lighting: lighting.* (lane F; docs/modern/PLAN.md F2, ARCHITECTURE.md section 7) (settings_table.h).
#include "settings_table.h"

namespace gt4::settings_table {

void lightingOptions(std::vector<Option>& o) {
    // lighting.night: enhanced has two forms. With render.scene = hybrid (nightModernWanted: the night's modern
    // path, PLAN.md N3, F7, B10's channels) the lamps light each surface's own colour, and GT4's baked lamp light
    // stays only where none of ours replaces it. Without hybrid it is F2's first form, added over GT4's baked light
    // from estimates. In no preset until hybrid is (the Odin's cost, the owner's look); --set or the settings file.
    o.push_back(enumOption("lighting.night", &Settings::night, kLighting,
        "original: the game's own night lighting; enhanced: the course's lamps light the road, the walls and the cars "
        "near them. With render.scene = hybrid they light each surface's own colour, and the game's baked lamp light "
        "stays only where no lamp replaces it; without, their light is added over the game's. Not in any preset yet."));
}

}  // namespace gt4::settings_table
