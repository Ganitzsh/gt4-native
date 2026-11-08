// Per-source defaults for the light gather (light_sources.h). Starting points: a street lamp's pool on the road is
// about 15-20 m across; head lamps reach further and are brighter; tail and brake lamps light only what is close.
// Tuned against reference stills of a night capture (PLAN.md F2's acceptance) once the game sends its lights (A7).
#include "light_sources.h"

#include "gt4/gfx.h"

namespace gt4::render {

SourceDefaults sourceDefaults(uint8_t source) {
    using namespace gfx::scene;
    switch (source) {
    case kSrcCourseGlow: return {18.0f, 6.0f, 0.3f, true, true};
    case kSrcHeadlight: return {40.0f, 20.0f, 0.1f, true, true};
    case kSrcTailLight: return {4.0f, 1.0f, 0.05f, true, true};
    case kSrcBrakeLight: return {5.0f, 2.0f, 0.05f, true, true};
    case kSrcCarLightReflection: return {0.0f, 0.0f, 0.0f, false, true};   // GT4's picture of a light, not a light
    case kSrcSun: return {0.0f, 1.0f, 0.0f, true, true};
    case kSrcWarning: return {6.0f, 2.0f, 0.1f, true, true};
    // A car lamp whose group is not known (A7 step 1a: entries[] keeps neither the group nor a direction): a point
    // light all round the car, so as small and dim as a tail lamp until step 2's headlight spots.
    case kSrcCarLamp: return {4.0f, 1.0f, 0.05f, true, true};
    case kSrcOther: return {10.0f, 2.0f, 0.2f, true, true};                // sent as "other" on purpose
    default: return {0.0f, 0.0f, 0.0f, false, false};
    }
}

}  // namespace gt4::render
