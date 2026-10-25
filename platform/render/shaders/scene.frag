#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "scene_common.glsl"
#include "sky.glsl"

layout(set = 0, binding = 2) uniform sampler2DArray albedoTexture;   // the 3D material sampler (filter, anisotropy, LOD bias)
layout(set = 0, binding = 3) uniform sampler2DShadow shadowMap;
layout(set = 0, binding = 4) uniform sampler2D shadowDepth;          // same image, raw depth (PCSS blocker search)
layout(set = 0, binding = 5) uniform samplerCube environment;

layout(location = 0) in vec3 inWorld;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inCurrent;
layout(location = 4) in vec4 inPrevious;
layout(location = 5) flat in int inInstance;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

// 16-point Poisson disc (unit radius).
const vec2 POISSON[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
    vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790));

float shadowTap(vec3 coord, vec2 offset) {
    return texture(shadowMap, vec3(coord.xy + offset, coord.z));
}

float shadowFactor(vec3 world, vec3 normal) {
    int mode = int(frame.shadowParams.x);
    if (mode == 0) {
        // Original: blob shadows under the cars, as a PS2 racing game would draw them.
        float shade = 1.0;
        for (int i = 0; i < 8; ++i) {
            vec4 blob = frame.blobs[i];
            if (blob.w <= 0.0) continue;
            float d = length((world.xz - blob.xy) / vec2(blob.z, blob.z * 2.2));
            shade *= 1.0 - blob.w * (1.0 - smoothstep(0.6, 1.0, d)) * step(world.y, 0.05);
        }
        return shade;
    }
    // Normal offset (in metres) against acne on surfaces at grazing light angles.
    vec4 light = frame.lightViewProjection * vec4(world + normal * 0.04, 1.0);
    vec3 coord = vec3(light.xy * 0.5 + 0.5, light.z);
    if (any(lessThan(coord, vec3(0.0))) || any(greaterThan(coord, vec3(1.0)))) return 1.0;
    coord.z -= frame.shadowParams.z;
    float texel = frame.shadowParams.y;
    if (mode == 1) return shadowTap(coord, vec2(0.0));
    if (mode == 2) {
        float sum = 0.0;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) sum += shadowTap(coord, vec2(x, y) * texel);
        return sum / 9.0;
    }
    // Per-pixel rotated Poisson disc; the rotation changes every frame so temporal AA integrates it.
    float angle = interleavedGradientNoise(gl_FragCoord.xy + 5.588238 * mod(frame.renderInfo.z, 64.0)) * 2.0 * PI;
    mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    float radius = 1.5 * texel;
    if (mode == 4) {
        // PCSS (Fernando 2005): average blocker depth, then a penumbra proportional to the distance.
        float blockers = 0.0, count = 0.0;
        float searchRadius = frame.shadowParams.w * 8.0 * texel;
        for (int i = 0; i < 16; ++i) {
            float depth = texture(shadowDepth, coord.xy + rotation * POISSON[i] * searchRadius).r;
            if (depth < coord.z) { blockers += depth; count += 1.0; }
        }
        if (count == 0.0) return 1.0;
        float blocker = blockers / count;
        float penumbra = (coord.z - blocker) / max(blocker, 1e-3) * frame.shadowParams.w;
        radius = clamp(penumbra * 40.0, 1.0, 12.0) * texel;
    }
    float sum = 0.0;
    for (int i = 0; i < 16; ++i) sum += shadowTap(coord, rotation * POISSON[i] * radius);
    return sum / 16.0;
}

// GGX / Trowbridge-Reitz distribution and height-correlated Smith visibility (Heitz 2014).
float distributionGgx(float nh, float a) {
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float visibilitySmith(float nv, float nl, float a) {
    float a2 = a * a;
    float v = nl * sqrt(nv * nv * (1.0 - a2) + a2);
    float l = nv * sqrt(nl * nl * (1.0 - a2) + a2);
    return 0.5 / max(v + l, 1e-5);
}
vec3 fresnelSchlick(vec3 f0, float vh) { return f0 + (1.0 - f0) * pow(1.0 - vh, 5.0); }
// Split-sum environment BRDF approximation (Karis 2014, after Lazarov 2013).
vec3 environmentBrdf(vec3 f0, float roughness, float nv) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

vec3 environmentSample(vec3 direction, float roughness) {
    float mips = frame.envParams.x;
    float lod = frame.envParams.y > 0.5 ? roughness * (mips - 1.0) : 0.0;
    return textureLod(environment, direction, lod).rgb * frame.envParams.z;
}

vec3 applyFog(vec3 color, vec3 world) {
    vec3 toPoint = world - frame.cameraPosition.xyz;
    float distance = length(toPoint);
    int mode = int(frame.fogParams.w);
    if (mode == 0) {
        // Original: linear distance fog with a constant colour, as the GS fog register does per vertex.
        float f = clamp((frame.fogColor.w - distance) / (frame.fogColor.w * 0.85), 0.0, 1.0);
        return mix(frame.fogColor.rgb * frame.sunColor.w, color, f);
    }
    // Exponential height fog, integrated analytically along the view ray (Wenzel 2006 / Quilez).
    vec3 direction = toPoint / max(distance, 1e-4);
    float density = frame.fogParams.x;
    float falloff = frame.fogParams.y;
    float startHeight = frame.cameraPosition.y - frame.fogParams.z;
    float fogAmount = density * exp(-falloff * startHeight);
    float slope = falloff * direction.y;
    float integral = abs(slope) > 1e-4 ? (1.0 - exp(-slope * distance)) / slope : distance;
    float transmittance = exp(-fogAmount * integral);
    vec3 fogLight = frame.fogColor.rgb * frame.sunColor.w;
    if (mode == 2) {
        float sun = pow(max(dot(direction, frame.sunDirection.xyz), 0.0), 12.0);
        fogLight += frame.sunColor.rgb * frame.sunDirection.w * sun * 0.25;
    }
    return color * transmittance + fogLight * (1.0 - transmittance);
}

void main() {
    Instance instance = instances[inInstance];
    vec3 normal = normalize(inNormal);
    if (!gl_FrontFacing) normal = -normal;
    vec3 view = normalize(frame.cameraPosition.xyz - inWorld);
    vec3 base = instance.color.rgb;
    float layer = instance.material.w;
    if (layer >= 0.0) base *= texture(albedoTexture, vec3(inUv, layer)).rgb;
    float roughness = clamp(instance.material.x, 0.03, 1.0);
    float metallic = instance.material.y;
    float clearcoat = instance.color.a;
    vec3 f0 = mix(vec3(0.04), base, metallic);
    vec3 diffuseColor = base * (1.0 - metallic);
    float a = roughness * roughness;

    vec3 l = frame.sunDirection.xyz;
    vec3 h = normalize(view + l);
    float nl = clamp(dot(normal, l), 0.0, 1.0);
    float nv = clamp(dot(normal, view), 1e-4, 1.0);
    float nh = clamp(dot(normal, h), 0.0, 1.0);
    float vh = clamp(dot(view, h), 0.0, 1.0);
    float shadow = shadowFactor(inWorld, normal);
    vec3 sunLight = frame.sunColor.rgb * frame.sunDirection.w;
    vec3 specular = distributionGgx(nh, a) * visibilitySmith(nv, nl, a) * fresnelSchlick(f0, vh);
    vec3 direct = (diffuseColor / PI + specular) * sunLight * nl * shadow;

    // Image-based ambient from the environment map (diffuse from its blurriest mip).
    vec3 irradiance = environmentSample(normal, 1.0);
    vec3 reflected = reflect(-view, normal);
    vec3 envSpecular = environmentSample(reflected, roughness) * environmentBrdf(f0, roughness, nv);
    float ambientOcclusion = mix(0.55, 1.0, shadow);
    vec3 ambient = (diffuseColor * irradiance + envSpecular) * ambientOcclusion;
    if (int(frame.shadowParams.x) == 0) ambient *= shadow;

    vec3 color = direct + ambient;
    if (clearcoat > 0.0) {
        // Clear-coat layer of car paint: a smooth dielectric lobe on top (Kulla & Conty / Filament model).
        const float coatRoughness = 0.04;
        float ca = coatRoughness * coatRoughness;
        float fc = 0.04 + 0.96 * pow(1.0 - vh, 5.0);
        vec3 coatDirect = vec3(distributionGgx(nh, ca) * visibilitySmith(nv, nl, ca) * fc) * sunLight * nl * shadow;
        float coatEnvFresnel = environmentBrdf(vec3(0.04), coatRoughness, nv).x;
        vec3 coatEnv = environmentSample(reflected, coatRoughness) * coatEnvFresnel;
        float attenuation = 1.0 - clearcoat * (0.04 + 0.96 * pow(1.0 - nv, 5.0));
        color = color * attenuation + clearcoat * (coatDirect + coatEnv);
    }
    float emissive = instance.material.z * (frame.renderInfo.w > 0.5 ? 4.0 : 1.0);
    color += base * emissive;
    color = applyFog(color, inWorld);

    // The colour target is RGBA16F: keep sharp highlights (GGX of a near-mirror under the sun reaches 1e5)
    // inside its range, and never write NaN, which would spread through every filter downstream.
    outColor = vec4(clamp(color, vec3(0.0), vec3(32768.0)), 1.0);
    if (any(isnan(color))) outColor.rgb = vec3(0.0);
    vec2 current = inCurrent.xy / inCurrent.w * 0.5 + 0.5;
    vec2 previous = inPrevious.xy / inPrevious.w * 0.5 + 0.5;
    outVelocity = previous - current;
}
