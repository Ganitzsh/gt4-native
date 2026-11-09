#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// The 2D layer's text (PLAN.md E5): glyphs as signed distance fields (platform/ui/host/sdf_font.cpp, 0.5 at the edge),
// anti-aliased over about one output pixel whatever the size, so text stays sharp from 720p to 4K.
layout(set = 0, binding = 0) uniform sampler2D atlas;   // r = distance (0.5 on the outline, more inside)
layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push { vec2 targetSize; float srgbTarget; float paperWhite; } push;   // as overlay.frag
void main() {
    float distance = texture(atlas, inUv).r;
    float width = max(fwidth(distance), 1e-4) * 0.7;
    float coverage = smoothstep(0.5 - width, 0.5 + width, distance);
    // Colours as overlay.frag: authored sRGB-encoded, blended in that space; HDR10 at paper white, PQ-encoded.
    vec3 color = push.srgbTarget > 0.5 ? srgbDecode(inColor.rgb) : inColor.rgb;
    if (push.paperWhite > 0.0) color = pqEncode(bt709ToBt2020(srgbDecode(inColor.rgb)) * push.paperWhite / 10000.0);
    outColor = vec4(color, inColor.a * coverage);
}
