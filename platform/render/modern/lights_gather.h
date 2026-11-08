#pragma once
// The light gather (ticket F2, docs/modern/CONTRACTS.md c.2): one view's gfx::scene::Light records into the light
// list (LightListHeader + GpuLight[]), for the cluster build (F1) and its readers (the GS-path night relight now, the
// deferred and forward passes later).
//
// The records are already in the view's space and in game units (= metres, spike R1; right-handed, +y up, the view
// looks down -z), which is the light list's convention, so positions and directions pass through. What the records
// leave open comes from the per-source table (light_sources.h): the range when a record has none, the intensity
// scale and the source radius. Colours: the record's is display-referred, 1.0 the full value, above 1 brighter before
// the game's clamp (CONTRACTS.md a.4, the Light record's `colour`), so it is linearised with 2.2 over its whole range
// (2.0 gives 2^2.2 = 4.59 times the light of 1.0), then times the record's intensity, the source's scale and the
// setting's scale. Directional lights come first (the header's directionalCount). Over kMaxLights point and spot
// lights, those whose sphere of `range` is nearest the camera are kept (max(0, distance - range), ties: the brighter).
//
// GpuLight::flags gets kGameGlow when the game drew a glow quad for the light (width or height > 0): a reader that
// adds glows of its own (bloom, lens effects) leaves those lights alone, since GT4's sprite is already in the image.
//
// GT4_NIGHT_TEST_LIGHTS=N (debugging, until the game sends its lights, PLAN.md A7) adds N synthetic street lamps to
// every view: two rows along a straight road ahead of the camera.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gt4/gfx.h"
#include "gt4/render/light_list.h"

namespace gt4::render {

struct GatherStats {
    uint32_t records = 0;         // the view's records
    uint32_t notLights = 0;       // sources that do not light (car-light reflections)
    uint32_t unknownSource = 0;   // a source value the table does not know (not lit)
    uint32_t invalid = 0;         // non-finite position, colour or intensity
    uint32_t dark = 0;            // no light this frame: black, intensity or scale 0 (an off lamp, a blink's dark half)
    uint32_t dropped = 0;         // over kMaxLights (the spheres farthest from the camera)
    uint32_t droppedAround = 0;   // ... of them, ones whose sphere holds the camera (a visible loss)
    uint32_t kept = 0;
};

// The records of `view` among `records`, into `out` (replaced) and a header for the cluster slices [zNear, zFar]
// (metres). `scale` multiplies every colour (the setting's strength).
LightListHeader gatherLights(const gfx::scene::Light* records, size_t count, uint16_t view, float zNear, float zFar,
                             float scale, std::vector<GpuLight>& out, GatherStats& stats);

// N synthetic street lamps for `view` (GT4_NIGHT_TEST_LIGHTS): two rows 7 m either side of the camera's axis, 4 m
// above it, every 12 m from 6 m ahead; warm white, kSrcCourseGlow, ids 1..N.
std::vector<gfx::scene::Light> testLights(uint16_t view, uint32_t n);

// GT4_NIGHT_TEST_LIGHTS, read once (0 when unset).
uint32_t testLightCount();

}  // namespace gt4::render
