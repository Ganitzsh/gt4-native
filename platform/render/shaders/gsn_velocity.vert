#version 460
// The native renderer's motion vectors (docs/NATIVE_RENDERER.md, "The scene stage"): a 3D draw of the scene
// again, its previous positions only. The position is gsn.vert's to the bit (invariant), so that a depth test
// EQUAL against the Z buffer the scene left keeps exactly the surfaces it shows.
#extension GL_GOOGLE_include_directive : require
#include "gsn_common.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in uint inZ;
layout(location = 7) in vec3 inMotion;   // the previous window position, homogeneous over this frame's w
layout(location = 8) in float inW;       // gsn.vert's

// perspective-correct times w, with w (as gsn.vert's 3D values: the same where the GPU clips)
layout(location = 0) out vec3 outMotionW;
layout(location = 1) out float outW;

invariant gl_Position;

void main() {
    gl_Position = gsnPosition(inPosition, inZ, inW);
    outMotionW = inMotion * gl_Position.w;
    outW = gl_Position.w;
}
