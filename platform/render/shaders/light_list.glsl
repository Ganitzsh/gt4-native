// The light list (docs/modern/CONTRACTS.md (c)), std430: the GLSL copy of include/gt4/render/light_list.h.
// Every field and function here has the same name and meaning there; tests/light_list.cpp checks the layouts
// and the functions against the C++ ones on a GPU. Readers declare their own buffers with these structs, e.g.
//   layout(std430, set = 0, binding = 0) readonly buffer Header { LightListHeader header; };
//   layout(std430, set = 0, binding = 1) readonly buffer Lights { GpuLight lights[]; };
#ifndef GT4_LIGHT_LIST_GLSL
#define GT4_LIGHT_LIST_GLSL

const uint kMaxLights = 1024u;
const uint kClusterX = 16u, kClusterY = 9u, kClusterZ = 24u;
const uint kMaxLightsPerCluster = 128u;
const uint kClusterCount = kClusterX * kClusterY * kClusterZ;

const uint kPoint = 0u, kSpot = 1u, kDirectional = 2u;                          // LightType
const uint kCastsShadow = 1u, kVolumetric = 2u, kNoSpecular = 4u, kGameGlow = 8u, kFromGame = 16u;   // LightFlags
const uint kProfileShift = 8u, kProfileMask = 0xFFu;                            // the flags' profile (F7), 0 none
const uint kProfileNone = 0u, kProfileStreetLamp = 1u;                          // LightProfile

struct GpuLight {          // 64 bytes
    vec3 position; float range;        // view space, metres
    vec3 direction; float spotScale;   // view space, unit, pointing away from the light
    vec3 colour; float spotOffset;     // linear Rec.709, scene-referred
    uint type; uint flags;
    float sourceRadius;
    uint id;
};

struct LightListHeader {   // 32 bytes
    uint count;
    uint directionalCount;
    float zNear, zFar;
    float sliceScale, sliceBias;
    uint clustersX, clustersY;
};

struct ClusterCell { uint offset; uint count; };

// cosAngle = dot(-L, direction), L the unit vector from the surface to the light.
float lightCone(float cosAngle, float scale, float offset) {
    const float c = clamp(cosAngle * scale + offset, 0.0, 1.0);
    return c * c;
}

// The intensity's factor at cosAngle for a profile (light_list.h): the street lamp's 1 / cos^3, held at
// kStreetLampMaxGain; 1 for none.
const float kStreetLampMaxGain = 16.0;
float lightProfile(uint profile, float cosAngle) {
    if (profile != kProfileStreetLamp) return 1.0;
    const float c = max(cosAngle, 1e-3);
    return min(1.0 / (c * c * c), kStreetLampMaxGain);
}

// saturate(1 - (d / range)^4)^2 / (d^2 + sourceRadius^2); 0 for a range that is not positive, the
// denominator at least kMinFalloffDenominator (light_list.h)
const float kMinFalloffDenominator = 1e-8;
float lightFalloff(float d, float range, float sourceRadius) {
    if (!(range > 0.0)) return 0.0;
    const float x = d / range;
    const float x2 = x * x;
    const float w = clamp(1.0 - x2 * x2, 0.0, 1.0);
    return w * w / max(d * d + sourceRadius * sourceRadius, kMinFalloffDenominator);
}

// The slice of view distance z (metres); before zNear slice 0, beyond zFar the last.
uint clusterSlice(float z, float scale, float bias) {
    const float s = floor(log(max(z, 1e-6)) * scale + bias);
    return uint(clamp(s, 0.0, float(kClusterZ - 1u)));
}

// Cells are stored x fastest, then y, then the slice.
uint clusterIndex(uvec3 cell, uint clustersX, uint clustersY) {
    return (cell.z * clustersY + cell.y) * clustersX + cell.x;
}

// A light's contribution factor at a view-space point p (metres), without the BRDF: falloff times cone for
// point and spot lights, 1 for directional ones. L is returned unit, from p towards the light.
float lightAttenuation(GpuLight light, vec3 p, out vec3 L) {
    if (light.type == kDirectional) {
        L = -light.direction;
        return 1.0;
    }
    const vec3 toLight = light.position - p;
    const float d = length(toLight);
    L = d > 0.0 ? toLight / d : vec3(0.0, 0.0, 1.0);
    float a = lightFalloff(d, light.range, light.sourceRadius);
    if (light.type == kSpot) {
        const float cosAngle = dot(-L, light.direction);
        a *= lightCone(cosAngle, light.spotScale, light.spotOffset) *
             lightProfile((light.flags >> kProfileShift) & kProfileMask, cosAngle);
    }
    return a;
}

#endif
