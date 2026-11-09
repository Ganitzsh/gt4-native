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

// The GS interpolates linearly across the screen (its STQ carry the perspective): so do these.
layout(location = 0) noperspective out vec4 outColour;
layout(location = 1) noperspective out vec3 outStq;
layout(location = 2) noperspective out vec2 outUv;
layout(location = 3) noperspective out float outFog;
layout(location = 4) flat out vec4 outBounds;
// 3D (homogeneous) draws: the same values, perspective-correct times w, with w (gsn.frag divides: sum l_i a_i w_i / w_i
// over sum l_i w_i / w_i, the screen-linear value). Where the GPU clips a triangle crossing w = 0 (behind the
// camera), drivers differ on noperspective attributes (lavapipe stretched them across the screen); every one
// clips these alike.
layout(location = 5) out vec4 outColourW;
layout(location = 6) out vec3 outStqW;
layout(location = 7) out float outFogW;
layout(location = 8) out float outW;
layout(location = 9) flat out uint outHomogeneous;
#if defined(GSN_EXACT)
// B24 (kGsExact): a sprite's min corner and its texture steps per pixel, (x0, y0, ds, dt), which GsnScene writes into
// the sprite's own S, T, Q and U in the GPU's copy (the front end's vertices keep their UVs); flat, so the provoking
// vertex's exact bits arrive. Other primitives and other modes: whatever S, T, Q and U hold, unread.
layout(location = 11) flat out vec4 outSprite;
#endif

// gsn_velocity.vert draws the scene's 3D again with the same position to the bit (its depth test EQUAL).
invariant gl_Position;

void main() {
    gl_Position = gsnPosition(inPosition, inZ, inW);
    outColour = inColour;
    outStq = inStq;
    outUv = inUv;
    outFog = float(inFog);
    outBounds = inBounds;
#if defined(GSN_EXACT)
    outSprite = vec4(inStq, inUv.x);
#endif
    const float w = gl_Position.w;
    outColourW = inColour * w;
    outStqW = inStq * w;
    outFogW = float(inFog) * w;
    outW = w;
    outHomogeneous = inW != 0.0 ? 1u : 0u;
    // GS points cover one GS pixel: scale x scale render pixels (point lists must write the size; undefined
    // otherwise, which lost the device in GT mode's map).
    gl_PointSize = max(draw.scale.x, draw.scale.y);
}
