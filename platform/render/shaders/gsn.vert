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

layout(location = 0) out vec4 outColour;
layout(location = 1) out vec3 outStq;
layout(location = 2) out vec2 outUv;
layout(location = 3) out float outFog;
layout(location = 4) flat out vec4 outBounds;

void main() {
    const vec2 p = (inPosition * draw.scale + 0.5) / draw.targetSize * 2.0 - 1.0;
    // Z as an unsigned fraction of 2^32: float keeps 24 bits, all of a 24-bit Z buffer.
    gl_Position = vec4(p, float(inZ) * (1.0 / 4294967296.0), 1.0);
    outColour = inColour;
    outStq = inStq;
    outUv = inUv;
    outFog = float(inFog);
    outBounds = inBounds;
    // GS points cover one GS pixel: scale x scale render pixels (point lists must write the size; undefined
    // otherwise, which lost the device in GT mode's map).
    gl_PointSize = max(draw.scale.x, draw.scale.y);
}
