// The light gather (ticket F2, platform/render/modern/lights_gather.h): Light records of one view into the light list.
// CPU only: conversion of each field, the per-source defaults, the view filter, what is left out (reflections,
// invalid records), directional lights first, the nearest kept over kMaxLights, and the synthetic test lamps.
#include "lights_gather.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "light_sources.h"

using namespace gt4;
using namespace gt4::render;
using gfx::scene::Light;

namespace {

int failures = 0;
void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what.c_str());
        ++failures;
    }
}
bool near(float a, float b, float tolerance = 1e-5f) { return std::fabs(a - b) <= tolerance * std::max(1.0f, std::fabs(b)); }

Light record(uint16_t view, uint8_t source, float x, float y, float z) {
    Light l{};
    l.prefix = {gfx::scene::kLight, gfx::scene::kVersion};
    l.view = view;
    l.kind = gfx::scene::kLightPoint;
    l.source = source;
    l.id = 7;
    l.entrant = 0xFFFFFFFFu;
    l.position[0] = x;
    l.position[1] = y;
    l.position[2] = z;
    l.colour[0] = 1.0f;
    l.colour[1] = 0.5f;
    l.colour[2] = 0.25f;
    l.intensity = 0.8f;
    return l;
}

void conversion() {
    Light l = record(2, gfx::scene::kSrcCourseGlow, 1.0f, 2.0f, -3.0f);
    l.width = 0.5f;
    std::vector<GpuLight> out;
    GatherStats s;
    const LightListHeader h = gatherLights(&l, 1, 2, 0.5f, 500.0f, 1.5f, out, s);
    expect(out.size() == 1 && s.kept == 1 && s.records == 1, "one record gives one light");
    if (out.empty()) return;
    const GpuLight& g = out[0];
    const SourceDefaults d = sourceDefaults(gfx::scene::kSrcCourseGlow);
    expect(g.type == kPoint, "a point record is a point light");
    expect(g.position[0] == 1.0f && g.position[1] == 2.0f && g.position[2] == -3.0f, "the position passes through");
    expect(near(g.colour[0], std::pow(1.0f, 2.2f) * 0.8f * d.intensity * 1.5f), "red linearised, times intensity, source, scale");
    expect(near(g.colour[1], std::pow(0.5f, 2.2f) * 0.8f * d.intensity * 1.5f), "green linearised");
    expect(near(g.colour[2], std::pow(0.25f, 2.2f) * 0.8f * d.intensity * 1.5f), "blue linearised");
    expect(g.range == d.range && g.sourceRadius == d.sourceRadius, "range and radius from the source table when the record has none");
    expect(g.flags == (kFromGame | kGameGlow), "flags: from the game, its glow drawn");
    expect(g.id == 7, "the id passes through");
    expect(h.count == 1 && h.directionalCount == 0 && h.zNear == 0.5f && h.zFar == 500.0f, "header counts and range");
    const LightListHeader m = makeHeader(1, 0, 0.5f, 500.0f);
    expect(h.sliceScale == m.sliceScale && h.sliceBias == m.sliceBias && h.clustersX == kClusterX, "header slices as makeHeader");

    l.width = 0.0f;
    l.range = 9.0f;
    gatherLights(&l, 1, 2, 0.5f, 500.0f, 1.0f, out, s);
    expect(out.size() == 1 && out[0].range == 9.0f, "a record's own range wins");
    expect(out.size() == 1 && out[0].flags == kFromGame, "no glow drawn: no kGameGlow");

    // Above 1 (brighter than white before the game's clamp, CONTRACTS.md a.4): the same 2.2 curve, 2.0 -> 4.59.
    l.colour[0] = 2.0f;
    l.colour[1] = 1.0f;
    l.intensity = 1.0f;
    gatherLights(&l, 1, 2, 0.5f, 500.0f, 1.0f, out, s);
    expect(out.size() == 1 && near(out[0].colour[0] / out[0].colour[1], 4.5948f, 1e-4f), "2.0 is 2^2.2 = 4.59 times 1.0");
}

void filtering() {
    std::vector<Light> r;
    r.push_back(record(1, gfx::scene::kSrcCourseGlow, 0, 0, -5));
    r.push_back(record(2, gfx::scene::kSrcCourseGlow, 0, 0, -5));                  // another view
    r.push_back(record(1, gfx::scene::kSrcCarLightReflection, 0, 0, -5));          // not a light
    Light nan = record(1, gfx::scene::kSrcCourseGlow, 0, 0, -5);
    nan.position[1] = std::nanf("");
    r.push_back(nan);
    Light black = record(1, gfx::scene::kSrcCourseGlow, 0, 0, -5);
    black.colour[0] = black.colour[1] = black.colour[2] = 0.0f;
    r.push_back(black);
    Light off = record(1, gfx::scene::kSrcCourseGlow, 0, 0, -5);
    off.intensity = 0.0f;
    r.push_back(off);
    r.push_back(record(1, 14, 0, 0, -5));                                               // a source the table doesn't know (14: unused)
    std::vector<GpuLight> out;
    GatherStats s;
    gatherLights(r.data(), r.size(), 1, 0.5f, 500.0f, 1.0f, out, s);
    expect(s.records == 6, "only view 1's records counted");
    expect(s.notLights == 1, "a car-light reflection is not a light");
    expect(s.unknownSource == 1, "an unknown source does not light, counted apart");
    {
        const SourceDefaults car = sourceDefaults(gfx::scene::kSrcCarLamp), other = sourceDefaults(gfx::scene::kSrcOther);
        expect(car.known && !car.lights && car.range == sourceDefaults(gfx::scene::kSrcTailLight).range &&
                   other.range == 10.0f,
               "an unknown-group car lamp keeps a tail lamp's reach for F9, unlit; 'other' its general reach");
    }
    {   // A7's car lamps (step 2's three and step 1a's kSrcCarLamp) arrive but don't light: the tail and brake lamps and
        // kSrcCarLamp until F9 tunes them, a headlight until the host gives it its beam (F7)
        std::vector<Light> lamps;
        for (uint8_t source : {gfx::scene::kSrcHeadlight, gfx::scene::kSrcTailLight, gfx::scene::kSrcBrakeLight})
            lamps.push_back(record(1, source, 0, 0, -5));
        lamps.push_back(record(1, gfx::scene::kSrcCarLamp, 0, 0, -5));
        std::vector<GpuLight> lit;
        GatherStats ls;
        gatherLights(lamps.data(), lamps.size(), 1, 0.5f, 500.0f, 1.0f, lit, ls);
        expect(ls.notLights == 4 && ls.kept == 0,
               "headlights, tail and brake lamps and kSrcCarLamp counted as not lights");
        // F7's low beam: a headlight lights once the host gives it its beam (a spot with its cone, the profile in the
        // record's reserved word), and carries the profile in its flags; the same record without the cone still doesn't
        Light beam = record(1, gfx::scene::kSrcHeadlight, 0, 0, -5);
        beam.kind = gfx::scene::kLightSpot;
        beam.direction[2] = -1.0f;
        beam.cosInner = 0.97f;
        beam.cosOuter = 0.87f;
        beam.range = 50.0f;
        beam.reserved = kProfileLowBeam;
        std::vector<Light> withBeam{lamps[0], beam};
        gatherLights(withBeam.data(), withBeam.size(), 1, 0.5f, 500.0f, 1.0f, lit, ls);
        expect(ls.notLights == 1 && ls.kept == 1 && lit.size() == 1 && lit[0].type == kSpot &&
                   ((lit[0].flags >> kProfileShift) & kProfileMask) == kProfileLowBeam,
               "a headlight with its beam lights, as a spot with the low-beam profile; one without doesn't");
        // F9: a brake lamp with its cone lights too, with no profile and the source's own range
        Light brake = record(1, gfx::scene::kSrcBrakeLight, 0, 0, -5);
        brake.kind = gfx::scene::kLightSpot;
        brake.direction[2] = 1.0f;
        brake.cosInner = 0.64f;
        brake.cosOuter = 0.09f;
        std::vector<Light> rear{lamps[2], brake};
        gatherLights(rear.data(), rear.size(), 1, 0.5f, 500.0f, 1.0f, lit, ls);
        expect(ls.notLights == 1 && ls.kept == 1 && lit.size() == 1 && lit[0].type == kSpot &&
                   ((lit[0].flags >> kProfileShift) & kProfileMask) == kProfileNone &&
                   lit[0].range == sourceDefaults(gfx::scene::kSrcBrakeLight).range,
               "a brake lamp with its cone lights, as a plain spot with the source's range; one without doesn't");
    }
    expect(s.invalid == 1, "a NaN position is invalid");
    expect(s.dark == 2, "black and intensity 0 are dark lamps, counted apart");
    expect(s.kept == 1 && out.size() == 1, "one light kept");
    gatherLights(r.data(), 1, 1, 0.5f, 500.0f, 0.0f, out, s);
    expect(s.dark == 1 && s.kept == 0, "the setting's strength 0: dark");
}

void spotsAndDirectional() {
    std::vector<Light> r;
    // (a lit source: the tail and brake lamps' spots don't light yet, and a headlight only with its beam)
    Light spotUnknown = record(1, gfx::scene::kSrcOther, 0, 0, -2);
    spotUnknown.kind = gfx::scene::kLightSpot;
    spotUnknown.direction[2] = -2.0f;   // not unit
    r.push_back(spotUnknown);
    Light spot = spotUnknown;
    spot.cosInner = 0.95f;
    spot.cosOuter = 0.8f;
    r.push_back(spot);
    Light spotNoDirection = spot;
    spotNoDirection.direction[2] = 0.0f;
    r.push_back(spotNoDirection);
    Light sun = record(1, gfx::scene::kSrcSun, 0, 0, 0);
    sun.kind = gfx::scene::kLightDirectional;
    sun.direction[1] = -3.0f;
    r.push_back(sun);
    std::vector<GpuLight> out;
    GatherStats s;
    const LightListHeader h = gatherLights(r.data(), r.size(), 1, 0.5f, 500.0f, 1.0f, out, s);
    expect(out.size() == 4 && h.directionalCount == 1, "four lights, one directional");
    if (out.size() != 4) return;
    expect(out[3].type == kPoint, "a spot with its cone but no direction lights as a point");
    expect(out[0].type == kDirectional && near(out[0].direction[1], -1.0f) && out[0].range == 0.0f, "directional first, unit, no range");
    expect(out[1].type == kPoint, "a spot without its cone lights as a point");
    expect(near(out[1].direction[2], -1.0f), "its direction made unit");
    expect(out[2].type == kSpot && near(out[2].spotScale, spotScale(0.95f, 0.8f)) && near(out[2].spotOffset, spotOffset(0.95f, 0.8f)),
           "a spot with its cone");
}

void overLimit() {
    std::vector<Light> r;
    const uint32_t n = kMaxLights + 76;
    for (uint32_t i = 0; i < n; ++i) {
        Light l = record(1, gfx::scene::kSrcCourseGlow, 0, 0, -1.0f - float((i * 37) % n));   // distances shuffled
        l.id = i;
        r.push_back(l);
    }
    std::vector<GpuLight> out;
    GatherStats s;
    gatherLights(r.data(), r.size(), 1, 0.5f, 5000.0f, 1.0f, out, s);
    expect(out.size() == kMaxLights && s.dropped == 76, "kMaxLights kept, the rest dropped");
    float farthest = 0.0f;
    for (const GpuLight& g : out) farthest = std::max(farthest, -g.position[2]);
    expect(near(farthest, float(kMaxLights)), "the nearest kept (the farthest kept is at kMaxLights m)");
    expect(s.droppedAround == 0, "none of the dropped spheres holds the camera");

    // Ranked by the sphere, not the centre: a head lamp 30 m away with a 40 m range is kept before tail lamps 5 m away
    // with a 4 m range (lit sources with those ranges in the record: kSrcTailLight doesn't light yet, kSrcHeadlight only with its beam).
    r.clear();
    for (uint32_t i = 0; i < kMaxLights; ++i) {
        Light t = record(1, gfx::scene::kSrcOther, 0, 0, -5.0f);
        t.range = 4.0f;
        t.id = 100 + i;
        r.push_back(t);
    }
    Light head = record(1, gfx::scene::kSrcOther, 0, 0, -30.0f);
    head.range = 40.0f;
    head.id = 1;
    r.push_back(head);
    gatherLights(r.data(), r.size(), 1, 0.5f, 500.0f, 1.0f, out, s);
    bool headKept = false;
    for (const GpuLight& g : out) headKept = headKept || g.id == 1;
    expect(headKept && s.dropped == 1, "the far head lamp whose sphere reaches the camera is kept, a tail lamp dropped");

    // Every sphere around the camera (a tie): the brighter kept, and the loss counted.
    r.clear();
    for (uint32_t i = 0; i <= kMaxLights; ++i) {
        Light g = record(1, gfx::scene::kSrcCourseGlow, 0, 0, -1.0f);
        g.id = i;
        g.intensity = i == 5 ? 0.01f : 1.0f;   // light 5 the dimmest
        r.push_back(g);
    }
    gatherLights(r.data(), r.size(), 1, 0.5f, 500.0f, 1.0f, out, s);
    bool dimKept = false;
    for (const GpuLight& g : out) dimKept = dimKept || g.id == 5;
    expect(!dimKept && s.dropped == 1 && s.droppedAround == 1, "a tie drops the dimmest, counted as around the camera");
}

void testLamps() {
    const std::vector<Light> t = testLights(3, 6);
    expect(t.size() == 6, "six test lamps");
    expect(t.size() == 6 && t[0].position[0] == -t[1].position[0] && t[0].position[2] == t[1].position[2], "in pairs either side");
    expect(t.size() == 6 && t[4].position[2] < t[2].position[2] && t[2].position[2] < t[0].position[2], "further ahead pair by pair");
    std::vector<GpuLight> out;
    GatherStats s;
    gatherLights(t.data(), t.size(), 3, 0.5f, 500.0f, 1.0f, out, s);
    expect(s.kept == 6, "all six gathered");
}

}  // namespace

int main() {
    conversion();
    filtering();
    spotsAndDirectional();
    overLimit();
    testLamps();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lights_gather: all checks passed\n");
    return 0;
}
