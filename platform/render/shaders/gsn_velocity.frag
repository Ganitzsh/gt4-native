#version 460
// Motion vectors (gsn_velocity.vert): where this pixel's surface was in the previous frame, less where it is
// now (unjittered), in GS pixels. A surface without a previous position (2D, objects new this frame) has none.
#extension GL_GOOGLE_include_directive : require
#include "gsn_common.glsl"

layout(location = 0) in vec3 inMotionW;   // times w, perspective-correct (gsn_velocity.vert)
layout(location = 1) in float inW;
layout(location = 0) out vec2 outVelocity;

void main() {
    const vec3 inMotion = inMotionW / inW;
    if (inMotion.z <= 1e-12) {
        outVelocity = vec2(0.0);
        return;
    }
    const vec2 now = (gl_FragCoord.xy - 0.5) / draw.scale - draw.jitter;
    outVelocity = inMotion.xy / inMotion.z - now;
}
