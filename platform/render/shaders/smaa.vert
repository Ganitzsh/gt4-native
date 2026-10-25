#version 460
#extension GL_GOOGLE_include_directive : require
// SMAA vertex stage for the three passes (SMAA_PASS 0 edges, 1 blending weights, 2 neighbourhood blending).
// Uses the reference implementation of Jimenez et al. 2012, "SMAA: Enhanced Subpixel Morphological
// Antialiasing" (third_party/smaa/SMAA.hlsl, its GLSL 4 path).
layout(push_constant) uniform Push { vec4 metrics; vec4 subsampleIndices; } push;
#define SMAA_RT_METRICS push.metrics
#define SMAA_GLSL_4 1
#define SMAA_PRESET_ULTRA 1
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 0
#include "smaa/SMAA.hlsl"

layout(location = 0) out vec2 outTexcoord;
layout(location = 1) out vec4 outOffset0;
layout(location = 2) out vec4 outOffset1;
layout(location = 3) out vec4 outOffset2;
layout(location = 4) out vec2 outPixcoord;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    outTexcoord = uv;
    vec4 offsets[3];
    offsets[0] = vec4(0.0); offsets[1] = vec4(0.0); offsets[2] = vec4(0.0);
    outPixcoord = vec2(0.0);
#if SMAA_PASS == 0
    SMAAEdgeDetectionVS(uv, offsets);
#elif SMAA_PASS == 1
    SMAABlendingWeightCalculationVS(uv, outPixcoord, offsets);
#else
    SMAANeighborhoodBlendingVS(uv, offsets[0]);
#endif
    outOffset0 = offsets[0];
    outOffset1 = offsets[1];
    outOffset2 = offsets[2];
}
