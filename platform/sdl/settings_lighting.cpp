// Night lighting: lighting.* (lane F; docs/modern/PLAN.md F2, ARCHITECTURE.md section 7) (settings_table.h).
#include "settings_table.h"

namespace gt4::settings_table {

void lightingOptions(std::vector<Option>& o) {
    // lighting.night: in no preset until N3 skips GT4's lit-road layer (PLAN.md F2), so no release lights the road
    // twice; --set or the settings file only.
    o.push_back(enumOption("lighting.night", &Settings::night, kLighting,
        "original: the game's own night lighting; enhanced: the course's lamps also light the road and the cars "
        "near them (GT4's 3D scenes relit from their Z buffer with the lamps the game sends; needs its scene "
        "records). Not in any preset yet."));
}

}  // namespace gt4::settings_table
