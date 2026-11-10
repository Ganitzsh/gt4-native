#pragma once
// The light list shared by the GS-path night relight (F2), the clustered deferred lighting and the clustered
// forward pass (F3, B22): docs/modern/CONTRACTS.md (c). One producer (the light gather), one cluster build
// (F1), many readers. The layouts are std430 and mirrored, field for field, by
// platform/render/shaders/light_list.glsl; tests/light_list.cpp checks both against each other on a GPU.
//
// The functions below are the only definitions of the attenuation, the spot cone and the cluster slicing:
// every reader on the GPU uses light_list.glsl's copies, every CPU reference (F1's) uses these.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gt4::render {

inline constexpr uint32_t kMaxLights = 1024;          // per view per frame
inline constexpr uint32_t kClusterX = 16, kClusterY = 9, kClusterZ = 24;   // froxels over the view's render extent
inline constexpr uint32_t kMaxLightsPerCluster = 128;
inline constexpr uint32_t kClusterCount = kClusterX * kClusterY * kClusterZ;

enum LightType : uint32_t { kPoint = 0, kSpot = 1, kDirectional = 2 };
enum LightFlags : uint32_t {
    kCastsShadow = 1,      // a shadow-casting light (the sun; later headlights)
    kVolumetric = 2,       // contributes to froxel fog
    kNoSpecular = 4,
    kGameGlow = 8,         // GT4 draws its own glow sprite for it: do not add one
    kFromGame = 16,        // from a scene::Light record (else synthesised by the host)
};
// The flags' bits 8-15: the light's profile (PLAN.md F7), 0 none. A profile shapes a spot's intensity by the angle
// from its axis, on top of its cone.
inline constexpr uint32_t kProfileShift = 8, kProfileMask = 0xFF;
enum LightProfile : uint32_t {
    kProfileNone = 0,
    kProfileStreetLamp = 1,   // a road luminaire: the ground under a lamp pointing down lit evenly out to its cone,
                              // and the same from any mounting height (lightProfile)
    kProfileLowBeam = 2,      // a headlight's dipped beam: the same light at every distance out to its range
                              // (lightProfile), on a road that answers light at a grazing angle (lightDiffuse)
};

struct GpuLight {                       // 64 bytes
    float position[3]; float range;     // view space, metres; range: where the light reaches 0
    float direction[3]; float spotScale;   // view space, unit, pointing away from the light; 1 / max(cosInner - cosOuter, 1e-4)
    float colour[3]; float spotOffset;  // linear Rec.709, scene-referred, intensity folded in; -cosOuter * spotScale
    uint32_t type, flags;               // LightType, LightFlags
    float sourceRadius;                 // metres (soft falloff near the source, specular size)
    uint32_t id;                        // scene::Light::id (temporal stability, debugging)
};
static_assert(sizeof(GpuLight) == 64);
static_assert(offsetof(GpuLight, range) == 12 && offsetof(GpuLight, direction) == 16 &&
              offsetof(GpuLight, spotScale) == 28 && offsetof(GpuLight, colour) == 32 &&
              offsetof(GpuLight, spotOffset) == 44 && offsetof(GpuLight, type) == 48 &&
              offsetof(GpuLight, flags) == 52 && offsetof(GpuLight, sourceRadius) == 56 &&
              offsetof(GpuLight, id) == 60);

struct LightListHeader {                // 32 bytes, then GpuLight lights[count]
    uint32_t count;                     // lights in the list
    uint32_t directionalCount;          // the first directionalCount lights are directional (not clustered)
    float zNear, zFar;                  // metres: the cluster slices' range
    float sliceScale, sliceBias;        // slice = floor(log(z) * sliceScale + sliceBias), z = view distance in metres
    uint32_t clustersX, clustersY;      // = kClusterX, kClusterY (shaders read these)
};
static_assert(sizeof(LightListHeader) == 32);

struct ClusterCell { uint32_t offset, count; };   // into the index list: uint32 light indices
static_assert(sizeof(ClusterCell) == 8);

// --- Spot cone -------------------------------------------------------------------------------------------
// cosInner, cosOuter: cosines of the full-intensity and the zero-intensity half-angles (cosInner >= cosOuter).
inline float spotScale(float cosInner, float cosOuter) { return 1.0f / std::max(cosInner - cosOuter, 1e-4f); }
inline float spotOffset(float cosInner, float cosOuter) { return -cosOuter * spotScale(cosInner, cosOuter); }

// cosAngle = dot(-L, direction), L the unit vector from the surface to the light.
inline float lightCone(float cosAngle, float scale, float offset) {
    const float c = std::clamp(cosAngle * scale + offset, 0.0f, 1.0f);
    return c * c;
}

// --- Profiles (F7) -----------------------------------------------------------------------------------------
// The intensity's factor for a profile at cosAngle = dot(-L, direction) and distance d (metres) from the light; 1 for
// none or one this build doesn't know.
// kProfileStreetLamp, a road luminaire as its designer sets it: the same light on the road whatever the mounting
// height, and even out to the cone's edge. A lamp at height h pointing down lights the ground at an angle from its
// axis with intensity x cos^3 / h^2 (the inverse square and the ground's own cosine). The 'batwing', 1 / cos^3, makes
// that intensity / h^2 everywhere inside the cone; (h / kStreetLampHeight)^2, with h = d x cos the lamp's height
// above the lit point, makes it intensity / kStreetLampHeight^2 for every lamp, so a tunnel's low lamps don't light
// the road several times brighter than a street's high ones (Clubman Stage Route 5 Reverse's tunnel: 3 times GT4's
// level with the batwing alone). Together: d^2 / (kStreetLampHeight^2 x cos), the cosine held at
// kStreetLampMinCos (about 75 degrees: past where such optics cut off, and the factor stays finite at the horizon).
inline constexpr float kStreetLampHeight = 8.0f;     // metres: the height at which the factor on the axis is 1
inline constexpr float kStreetLampMinCos = 0.25f;
// kProfileLowBeam, a headlight's dipped beam: its optics put the most intensity just under the cut-off, where the
// road is farthest, so what it lights is about as bright far as near; the inverse square is cancelled out to the
// light's range (its window still ends it): (d / kLowBeamReference)^2. Without it a wall 10 m ahead is lit a hundred
// times more than the road 30 m ahead, and with no tone map between the two one of them is black or the other white.
inline constexpr float kLowBeamReference = 10.0f;   // metres: the distance at which the factor is 1
inline float lightProfile(uint32_t profile, float cosAngle, float d) {
    if (profile == kProfileStreetLamp)
        return d * d / (kStreetLampHeight * kStreetLampHeight * std::max(cosAngle, kStreetLampMinCos));
    if (profile == kProfileLowBeam) return d * d / (kLowBeamReference * kLowBeamReference);
    return 1.0f;
}

// The diffuse term for ndl = dot(n, L): Lambert's max(ndl, 0), except under the low beam, whose light reaches the road
// at a few degrees: a road isn't Lambertian there (its luminance coefficient rises towards grazing incidence, which
// road-lighting practice tabulates), so the term is wrapped, (ndl + kLowBeamWrap) / (1 + kLowBeamWrap): the road ahead
// gets about a quarter of what a surface facing the lamp gets, not a twentieth.
inline constexpr float kLowBeamWrap = 0.3f;
inline float lightDiffuse(uint32_t profile, float ndl) {
    if (profile == kProfileLowBeam) return std::clamp((ndl + kLowBeamWrap) / (1.0f + kLowBeamWrap), 0.0f, 1.0f);
    return std::max(ndl, 0.0f);
}

// --- Distance attenuation --------------------------------------------------------------------------------
// saturate(1 - (d / range)^4)^2 / (d^2 + sourceRadius^2): inverse square, windowed to 0 at range.
// Guarded: a range that is not positive (or NaN) gives 0, and the denominator is at least kMinFalloffDenominator
// (a point light without a source radius, sampled at its own position, stays finite).
inline constexpr float kMinFalloffDenominator = 1e-8f;
inline float lightFalloff(float d, float range, float sourceRadius) {
    if (!(range > 0.0f)) return 0.0f;
    const float x = d / range;
    const float x2 = x * x;
    const float w = std::clamp(1.0f - x2 * x2, 0.0f, 1.0f);
    return w * w / std::max(d * d + sourceRadius * sourceRadius, kMinFalloffDenominator);
}

// --- Cluster slices --------------------------------------------------------------------------------------
inline float clusterSliceScale(float zNear, float zFar) { return float(kClusterZ) / std::log(zFar / zNear); }
inline float clusterSliceBias(float zNear, float zFar) {
    return -float(kClusterZ) * std::log(zNear) / std::log(zFar / zNear);
}
// The slice of view distance z (metres). Distances before zNear go to slice 0, beyond zFar to the last.
inline uint32_t clusterSlice(float z, float scale, float bias) {
    const float s = std::floor(std::log(std::max(z, 1e-6f)) * scale + bias);
    return uint32_t(std::clamp(s, 0.0f, float(kClusterZ - 1)));
}
// Cells are stored x fastest, then y, then the slice.
inline uint32_t clusterIndex(uint32_t x, uint32_t y, uint32_t slice, uint32_t clustersX = kClusterX,
                             uint32_t clustersY = kClusterY) {
    return (slice * clustersY + y) * clustersX + x;
}

// A header for a view, with the slice parameters filled in from zNear and zFar.
inline LightListHeader makeHeader(uint32_t count, uint32_t directionalCount, float zNear, float zFar) {
    return {count, directionalCount, zNear, zFar, clusterSliceScale(zNear, zFar), clusterSliceBias(zNear, zFar),
            kClusterX, kClusterY};
}

}  // namespace gt4::render
