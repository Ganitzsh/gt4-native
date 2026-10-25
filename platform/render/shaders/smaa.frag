#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(push_constant) uniform Push { vec4 metrics; vec4 subsampleIndices; } push;
#define SMAA_RT_METRICS push.metrics
#define SMAA_GLSL_4 1
#define SMAA_PRESET_ULTRA 1
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#include "smaa/SMAA.hlsl"

layout(location = 0) in vec2 inTexcoord;
layout(location = 1) in vec4 inOffset0;
layout(location = 2) in vec4 inOffset1;
layout(location = 3) in vec4 inOffset2;
layout(location = 4) in vec2 inPixcoord;

#if SMAA_PASS == 0
// Luma edge detection on the sRGB-encoded image (as SMAA requires for luma edges).
layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(location = 0) out vec2 outEdges;
void main() {
    vec4 offsets[3] = vec4[](inOffset0, inOffset1, inOffset2);
    outEdges = SMAALumaEdgeDetectionPS(inTexcoord, offsets, colorTex);
}
#elif SMAA_PASS == 1
layout(set = 0, binding = 0) uniform sampler2D edgesTex;
layout(set = 0, binding = 1) uniform sampler2D areaTex;
layout(set = 0, binding = 2) uniform sampler2D searchTex;   // point sampled
layout(location = 0) out vec4 outWeights;
void main() {
    vec4 offsets[3] = vec4[](inOffset0, inOffset1, inOffset2);
    outWeights = SMAABlendingWeightCalculationPS(inTexcoord, inPixcoord, offsets, edgesTex, areaTex, searchTex,
                                                 push.subsampleIndices);
}
#else
// Neighbourhood blending, as SMAANeighborhoodBlendingPS but blending in linear light: the reference reads
// an sRGB view so the bilinear mix is linear; our intermediate is floating point, so the two texels are
// decoded and mixed here. Alpha carries the velocity length for the T2x reprojection weight.
layout(set = 0, binding = 0) uniform sampler2D colorTex;      // sRGB-encoded values
layout(set = 0, binding = 1) uniform sampler2D blendTex;
layout(set = 0, binding = 2) uniform sampler2D velocityTex;   // previous uv - current uv
layout(location = 0) out vec4 outColor;

vec3 bilinearLinear(vec2 uv) {
    vec2 size = push.metrics.zw;
    vec2 position = uv * size - 0.5;
    ivec2 i = ivec2(floor(position));
    vec2 f = position - vec2(i);
    ivec2 limit = ivec2(size) - 1;
    vec3 a = srgbDecode(texelFetch(colorTex, clamp(i, ivec2(0), limit), 0).rgb);
    vec3 b = srgbDecode(texelFetch(colorTex, clamp(i + ivec2(1, 0), ivec2(0), limit), 0).rgb);
    vec3 c = srgbDecode(texelFetch(colorTex, clamp(i + ivec2(0, 1), ivec2(0), limit), 0).rgb);
    vec3 d = srgbDecode(texelFetch(colorTex, clamp(i + ivec2(1, 1), ivec2(0), limit), 0).rgb);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec4 a;
    a.x = texture(blendTex, inOffset0.xy).a;
    a.y = texture(blendTex, inOffset0.zw).g;
    a.wz = texture(blendTex, inTexcoord).xz;
    float velocityLength = sqrt(5.0 * length(textureLod(velocityTex, inTexcoord, 0.0).rg));
    if (dot(a, vec4(1.0)) < 1e-5) {
        outColor = vec4(textureLod(colorTex, inTexcoord, 0.0).rgb, velocityLength);
        return;
    }
    bool h = max(a.x, a.z) > max(a.y, a.w);
    vec4 blendingOffset = h ? vec4(a.x, 0.0, a.z, 0.0) : vec4(0.0, a.y, 0.0, a.w);
    vec2 blendingWeight = h ? a.xz : a.yw;
    blendingWeight /= dot(blendingWeight, vec2(1.0));
    vec4 blendingCoord = blendingOffset * vec4(push.metrics.xy, -push.metrics.xy) + inTexcoord.xyxy;
    vec3 color = blendingWeight.x * bilinearLinear(blendingCoord.xy) + blendingWeight.y * bilinearLinear(blendingCoord.zw);
    outColor = vec4(srgbEncode(color), velocityLength);
}
#endif
