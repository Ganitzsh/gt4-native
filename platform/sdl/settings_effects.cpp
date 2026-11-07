// Effects and post-processing: effects.*, display.race_frames, post.* (settings_table.h).
#include "settings_table.h"

namespace gt4::settings_table {

// (display.race_frames after the effects: where the settings file has always had it)
void effectsOptions(std::vector<Option>& o) {
    o.push_back(enumOption("effects.shadows", &Settings::shadows, kEffects,
        "Shadow maps: low 1024 px 2x2 PCF, medium 2048 px 3x3 PCF, high 2048 px 16-tap Poisson, ultra 4096 px "
        "with contact-hardening (PCSS)."));
    o.push_back(enumOption("effects.reflections", &Settings::reflections, kEffects,
        "Environment reflections on cars: original = one sharp environment map; low..ultra = cube map of "
        "64..512 px prefiltered for roughness (GGX); off = a dimmed sharp map only."));
    o.push_back(boolOption("effects.road_reflections", &Settings::roadReflections,
        "Screen-space reflections on the road and other grey surfaces facing up, at effects.reflections' "
        "level. Off by default: GT4's courses are dry and the game draws none there."));
    o.push_back(enumOption("effects.bloom", &Settings::bloom, kEffects,
        "Bloom/glare: original = the PS2-style glare (bright pass over 3 levels, added); low..ultra = physically "
        "based bloom (energy-conserving mip chain, no threshold) over 4..8 levels."));
    o.push_back(enumOption("effects.dof", &Settings::dof, kEffects,
        "Depth of field: thin-lens circle of confusion with a golden-angle bokeh gather, 16..96 samples."));
    o.push_back(enumOption("effects.motion_blur", &Settings::motionBlur, kEffects,
        "Motion blur: per-pixel reconstruction filter with tile max velocity (McGuire et al. 2012), 8..24 samples."));
    o.push_back(enumOption("effects.heat_haze", &Settings::heatHaze, kEffects,
        "Heat haze (screen-space refraction over hot air and exhausts). Schema only: applied by the R1 game "
        "renderer; the test scene has none."));
    o.push_back(enumOption("effects.particles", &Settings::particles, kEffects,
        "Particles (smoke, sparks, dust): count and soft-particle depth fade. Schema only: applied by the R1 "
        "game renderer; the test scene has none."));
    o.push_back(enumOption("effects.sky_fog", &Settings::skyFog, kEffects,
        "Sky and fog: original = linear distance fog (the GS fog of the PS2); low = exponential height fog; "
        "medium..ultra = height fog with sun in-scattering."));
    o.push_back(enumOption("effects.ambient_occlusion", &Settings::ambientOcclusion, kEffects,
        "Ambient occlusion (an enhancement; the PS2 had none): the game's 3D scenes darkened where geometry "
        "occludes the sky, from their Z buffer; low to ultra take 4, 6, 8 or 12 directions and darken more. "
        "original and off: none."));
    o.push_back(enumOption("effects.lighting", &Settings::lighting, kLighting,
        "original: the game's own colours; enhanced: its 3D scenes graded as a modern renderer's filmic "
        "response (more contrast around middle grey, a soft highlight shoulder, vibrance, cool shadows and "
        "warm highlights), before its HUD."));
    o.push_back(enumOption("effects.mirror", &Settings::mirror, kMirror,
        "The rear-view mirror: original (the PS2's: no course detail objects, the lowest texture level, low-detail "
        "cars) or high (the course's detail objects, textures at their own level and cars as the main view draws them)."));
    o.push_back(enumOption("effects.model_detail", &Settings::modelDetail, kModelDetail,
        "Detail at distance: original (the PS2's), high or ultra keep each model's detailed levels (cars, scenery) "
        "twice or four times as far before the simpler ones take over."));
    o.push_back(enumOption("display.race_frames", &Settings::raceFrames, kRaceFrames,
        "Races: fields (the PS2's usual 480i: each frame draws every other line, 224 of them, and the picture "
        "alternates) or progressive (the game's own 480p mode: whole 448-line frames, twice the vertical detail). The game reads it as a race "
        "starts: a change applies from the next race."));
    o.push_back(enumOption("post.tonemapper", &Settings::tonemapper, kTonemappers,
        "HDR to display: original (clamp, as the PS2), reinhard (luminance), aces (Hill fit), agx and "
        "agx_punchy (Sobotka, Wrensch fit), pbr_neutral (Khronos PBR Neutral)."));
    o.push_back(floatOption("post.exposure", &Settings::exposure, -6.0f, 6.0f, "Exposure compensation in EV."));
    o.push_back(boolOption("post.auto_exposure", &Settings::autoExposure,
        "Automatic exposure from a luminance histogram, with smooth eye adaptation."));
    o.push_back(floatOption("post.bloom_intensity", &Settings::bloomIntensity, 0.0f, 1.0f,
        "Bloom mix (fraction of energy scattered by the lens)."));
    o.push_back(floatOption("post.film_grain", &Settings::filmGrain, 0.0f, 1.0f, "Film grain strength (0 = off)."));
    o.push_back(floatOption("post.vignette", &Settings::vignette, 0.0f, 1.0f, "Vignette strength (0 = off)."));
    o.push_back(floatOption("post.chromatic_aberration", &Settings::chromaticAberration, 0.0f, 1.0f,
        "Lateral chromatic aberration strength (0 = off)."));
    o.push_back(boolOption("post.dither", &Settings::dither,
        "Triangular-noise dither before quantising to the display format (removes banding)."));
    o.push_back(floatOption("post.shutter", &Settings::shutter, 0.0f, 1.0f,
        "Motion blur exposure time as a fraction of the frame (0.5 = a 180-degree shutter)."));
    o.push_back(floatOption("post.aperture", &Settings::aperture, 0.5f, 22.0f,
        "Depth of field f-number. The focal length follows from the field of view on a 24 mm tall sensor, so "
        "wide views have little blur and narrow (replay, telephoto) views a lot; smaller numbers blur more."));
    o.push_back(floatOption("post.focus_distance", &Settings::focusDistance, 0.0f, 10000.0f,
        "Depth of field focus distance in metres; 0 = autofocus on the centre of the screen."));
}

}  // namespace gt4::settings_table
