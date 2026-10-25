#version 460
#extension GL_GOOGLE_include_directive : require
#include "scene_common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorld;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUv;
layout(location = 3) out vec4 outCurrent;    // unjittered clip position
layout(location = 4) out vec4 outPrevious;   // previous frame clip position
layout(location = 5) flat out int outInstance;

void main() {
    Instance instance = instances[gl_InstanceIndex];
    vec4 world = instance.model * vec4(inPosition, 1.0);
    vec4 previousWorld = instance.previousModel * vec4(inPosition, 1.0);
    outWorld = world.xyz;
    outNormal = normalize(transpose(inverse(mat3(instance.model))) * inNormal);
    outUv = inUv * instance.uv.xy;
    outCurrent = frame.viewProjectionUnjittered * world;
    outPrevious = frame.previousViewProjection * previousWorld;
    outInstance = gl_InstanceIndex;
    gl_Position = frame.viewProjection * world;
}
