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
    // A headlight (A7 step 2) lights once it has its beam (F7's low beam: the host gives the record its cone and
    // profile on the night's modern path, GsnScene::lampsOf); as the game sends it, without a cone, the gather counts
    // it under notLights: a point light at the lamp would light all round the car.
    case kSrcHeadlight: return {50.0f, 20.0f, 0.1f, true, true};
    // The tail and brake lamps (A7 step 2; F9): as the headlights, lit once the host gives the record its cone (the
    // night's modern path, GsnScene::lampsOf: a wide spot backwards); without one the gather counts them under
    // notLights. Short ranges and low strengths: a tail lamp tints what is right behind the car, a brake lamp reaches
    // the car following.
    case kSrcTailLight: return {4.0f, 0.6f, 0.05f, true, true};
    case kSrcBrakeLight: return {8.0f, 2.0f, 0.05f, true, true};
    case kSrcCarLightReflection: return {0.0f, 0.0f, 0.0f, false, true};   // GT4's picture of a light, not a light
    case kSrcSun: return {0.0f, 1.0f, 0.0f, true, true};
    case kSrcWarning: return {6.0f, 2.0f, 0.1f, true, true};
    // A car lamp whose group is not known (A7 step 1a: entries[] keeps neither the group nor a direction): it could
    // only be a point light all round the car, and the rear lamps' saturated red lit the road ahead of a car as much
    // as behind it (the owner's playtest, 2026-11-09: their own car's lamps tinting the road in the bumper view). Not
    // lit until F9 tunes car lamps from A7 step 2's records (at the same points) and decides whether this one stays;
    // the records still arrive, counted under notLights; GT4's own lamp glows are drawn as before.
    case kSrcCarLamp: return {4.0f, 1.0f, 0.05f, false, true};
    // A course billboard (A7 step 1b): a candidate, not a light. F2b's lamp pass promotes the glows to kSrcCourseGlow;
    // what is left (trees, signs) never lights.
    case kSrcBillboard: return {0.0f, 0.0f, 0.0f, false, true};
    case kSrcOther: return {10.0f, 2.0f, 0.2f, true, true};                // sent as "other" on purpose
    default: return {0.0f, 0.0f, 0.0f, false, false};
    }
}

}  // namespace gt4::render
