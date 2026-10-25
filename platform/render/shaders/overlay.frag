#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set = 0, binding = 0) uniform sampler2D atlas;   // a = coverage (font + a white texel)
layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push { vec2 targetSize; float srgbTarget; } push;
void main() {
    float coverage = texture(atlas, inUv).a;
    // UI colours are authored sRGB-encoded and blended in that space, like the PS2's 2D layer.
    vec3 color = push.srgbTarget > 0.5 ? srgbDecode(inColor.rgb) : inColor.rgb;
    outColor = vec4(color, inColor.a * coverage);
}
