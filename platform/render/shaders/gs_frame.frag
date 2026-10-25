#version 460
// The PS2's displayed frame (the GS back end's CRTC output) as the scene: the image is an
// R8G8B8A8_SRGB texture, so sampling returns linear values and the pipeline's display transform and
// output encoding give the original 8-bit values back (the "original" preset shows it unaltered).
// No motion vectors: the GS frame has none.
layout(set = 0, binding = 0) uniform sampler2D frame;
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

void main() {
    outColor = vec4(textureLod(frame, inUv, 0.0).rgb, 1.0);
    outVelocity = vec2(0.0);
}
