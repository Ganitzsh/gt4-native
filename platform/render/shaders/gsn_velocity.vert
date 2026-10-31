#version 460
// The native renderer's motion vectors (docs/NATIVE_RENDERER.md, "The scene stage"): a 3D draw of the scene
// again, its previous positions only. The position is gsn.vert's to the bit (invariant), so that a depth test
// EQUAL against the Z buffer the scene left keeps exactly the surfaces it shows.
#extension GL_GOOGLE_include_directive : require
#include "gsn_common.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in uint inZ;
layout(location = 7) in vec3 inMotion;   // the previous window position, homogeneous over this frame's w

layout(location = 0) noperspective out vec3 outMotion;

invariant gl_Position;

void main() {
    const vec2 p = ((inPosition + draw.jitter) * draw.scale + 0.5) / draw.targetSize * 2.0 - 1.0;
    gl_Position = vec4(p, float(inZ) * (1.0 / 4294967296.0), 1.0);
    outMotion = inMotion;
}
