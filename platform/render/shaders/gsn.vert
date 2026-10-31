#version 460
// The native renderer's vertices: GS window coordinates (pixels) to clip space at the render scale.
// The GS covers a pixel when its top-left corner is inside a primitive, Vulkan when its centre is: shifted
// by half a render pixel, Vulkan samples at the GS's points (each GS pixel's at scale x scale positions).
#extension GL_GOOGLE_include_directive : require
#include "gsn_common.glsl"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in uint inZ;
layout(location = 2) in vec4 inColour;   // R8G8B8A8_UNORM: the GS's 0..255 over 255
layout(location = 3) in vec3 inStq;
layout(location = 4) in vec2 inUv;
layout(location = 5) in uint inFog;
layout(location = 6) in vec4 inBounds;   // a sprite's texture rectangle (min U, min V, max U, max V)
layout(location = 8) in float inW;       // 3D: the position is homogeneous (times w, Z's float bits); 0: not

// The GS interpolates linearly across the screen (its STQ carry the perspective): so do these, also where the
// GPU clips a homogeneous triangle.
layout(location = 0) noperspective out vec4 outColour;
layout(location = 1) noperspective out vec3 outStq;
layout(location = 2) noperspective out vec2 outUv;
layout(location = 3) noperspective out float outFog;
layout(location = 4) flat out vec4 outBounds;

// gsn_velocity.vert draws the scene's 3D again with the same position to the bit (its depth test EQUAL).
invariant gl_Position;

void main() {
    gl_Position = gsnPosition(inPosition, inZ, inW);
    outColour = inColour;
    outStq = inStq;
    outUv = inUv;
    outFog = float(inFog);
    outBounds = inBounds;
    // GS points cover one GS pixel: scale x scale render pixels (point lists must write the size; undefined
    // otherwise, which lost the device in GT mode's map).
    gl_PointSize = max(draw.scale.x, draw.scale.y);
}
