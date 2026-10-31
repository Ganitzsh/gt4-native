#version 460
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Output pass into the swapchain, inside the content rectangle (black bars are cleared around it):
// lens effects that belong after upscaling (chromatic aberration, vignette, film grain), then dither
// and the encoding the swapchain format needs.
layout(set = 0, binding = 0) uniform sampler2D image;   // output resolution, sRGB-encoded, linear clamp
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Push {
    vec2 size;              // content size in pixels
    float chromaticAberration;
    float vignette;
    float grain;
    float dither;           // 0 off, otherwise 1 / (2^bits - 1)
    float frame;
    float srgbTarget;       // 1: the swapchain format encodes sRGB itself, write linear
    float paperWhite;       // HDR10 output (PQ, BT.2020): the nits of SDR white; 0: SDR output
    float peak;             // ... the display's peak, nits: SDR highlights expand towards it
} push;

// HDR10: the SDR picture (linear, 0..1) in nits. Up to a knee at half of white it keeps SDR's look at paper white;
// above, its highlights expand smoothly (no kink: the added term starts flat) to the display's peak at white,
// scaling the colour by its luminance's gain so that hues stay.
vec3 toNits(vec3 c) {
    const float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    const float knee = 0.5;
    const float t = max(l - knee, 0.0) / (1.0 - knee);
    const float target = push.paperWhite * l + max(push.peak - push.paperWhite, 0.0) * t * t;
    return l > 1e-6 ? c * (target / l) : c * push.paperWhite;
}

vec3 fetchLinear(vec2 uv) { return srgbDecode(textureLod(image, uv, 0.0).rgb); }

void main() {
    vec2 uv = inUv;
    vec3 color;
    if (push.chromaticAberration > 0.0) {
        // Lateral aberration grows with the distance from the centre: red and blue focus at
        // slightly different magnifications.
        vec2 d = uv - 0.5;
        float k = push.chromaticAberration * 0.012;
        color.r = fetchLinear(0.5 + d * (1.0 + k)).r;
        color.g = fetchLinear(uv).g;
        color.b = fetchLinear(0.5 + d * (1.0 - k)).b;
    } else {
        color = fetchLinear(uv);
    }
    if (push.vignette > 0.0) {
        // Natural cos^4 falloff of a lens, on a circle that fits the content's aspect ratio.
        vec2 d = (uv - 0.5) * vec2(push.size.x / push.size.y, 1.0);
        float c = cos(clamp(length(d) * push.vignette * 1.1, 0.0, 1.5));
        color *= c * c * c * c;
    }
    if (push.grain > 0.0) {
        // Film grain: zero-mean noise whose amplitude follows the square root of the signal
        // (photon shot noise), new every frame.
        uint seed = uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u + uint(push.frame) * 26699u;
        float n = (hashToFloat(pcgHash(seed)) + hashToFloat(pcgHash(seed ^ 0x9e3779b9u)) - 1.0);
        color = max(color + n * push.grain * 0.08 * sqrt(max(color, vec3(0.0))), vec3(0.0));
    }
    const bool hdr = push.paperWhite > 0.0;
    vec3 encoded = hdr ? pqEncode(max(bt709ToBt2020(toNits(color)), vec3(0.0)) / 10000.0) : srgbEncode(color);
    if (push.dither > 0.0) {
        // Triangular-distributed dither of +-1 quantisation step (Gjoel 2016, "Banding in Games").
        uint seed = uint(gl_FragCoord.x) * 7919u + uint(gl_FragCoord.y) * 104729u + uint(push.frame) * 15485863u;
        vec3 n = vec3(hashToFloat(pcgHash(seed)), hashToFloat(pcgHash(seed + 1u)), hashToFloat(pcgHash(seed + 2u)));
        vec3 m = vec3(hashToFloat(pcgHash(seed + 3u)), hashToFloat(pcgHash(seed + 4u)), hashToFloat(pcgHash(seed + 5u)));
        encoded += (n + m - 1.0) * push.dither;
    }
    encoded = clamp(encoded, 0.0, 1.0);
    outColor = vec4(push.srgbTarget > 0.5 && !hdr ? srgbDecode(encoded) : encoded, 1.0);
}
