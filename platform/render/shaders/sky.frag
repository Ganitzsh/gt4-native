#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "scene_common.glsl"
#include "sky.glsl"

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

void main() {
    // The pixel samples the unjittered position (centre - jitter); rebuild that view ray.
    vec2 uv = (gl_FragCoord.xy - frame.jitter.xy) / frame.renderInfo.xy;
    // Reverse Z with an infinite far plane: depth 1 is the near plane, 0 is infinity, so use 1 and 0.5.
    vec4 near = frame.inverseViewProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 middle = frame.inverseViewProjection * vec4(uv * 2.0 - 1.0, 0.5, 1.0);
    vec3 direction = normalize(middle.xyz / middle.w - near.xyz / near.w);
    vec3 color = skyRadiance(direction, frame.sunDirection.xyz, frame.sunColor.w, true);
    outColor = vec4(color, 1.0);
    // Points at infinity move only with the camera rotation.
    vec4 previous = frame.previousViewProjection * vec4(direction, 0.0);
    outVelocity = (previous.xy / previous.w * 0.5 + 0.5) - uv;
}
