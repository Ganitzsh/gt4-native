#pragma once
// Per-source defaults for the light gather (ticket F2, docs/modern/CONTRACTS.md c.1): what the game's Light records
// do not say. Their range (0 = unknown in a record), the intensity the game's colour is scaled by into scene-referred
// linear light, the source's radius, and whether a source lights the scene at all (a car-light reflection is
// GT4's picture of a light on the road, not a light). Values are starting points, tuned against reference stills
// under lighting.night.
#include <cstdint>

namespace gt4::render {

struct SourceDefaults {
    float range;          // metres: where the light reaches 0, when the record has no range
    float intensity;      // the game's colour x intensity, linearised, times this
    float sourceRadius;   // metres
    bool lights;          // false: the record is not a light source (kept out of the light list)
    bool known;           // false: a source value this table does not know (does not light; counted apart)
};

// By gfx::scene::LightSource. A value the table does not know does not light, so a new kind shows up in the
// gather's stats (GatherStats::unknownSource) before it lights anything at values that look tuned.
SourceDefaults sourceDefaults(uint8_t source);

}  // namespace gt4::render
