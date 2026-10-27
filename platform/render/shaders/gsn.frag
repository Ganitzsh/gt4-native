#version 460
// The native renderer's pixels: the GS's texture function, fog, alpha test and blend.
// Targets hold colour / 255 and alpha / 128 (R16G16B16A16 float), so that blend factors keep the PS2's
// range (0x80 = 1.0, up to ~2.0). The blend ((A - B) * C >> 7) + D is written as Cs * s + Cd * d: the shader
// outputs Cs * s (source 0) and d (source 1, dual-source blending); with C = Ad the pipeline's fixed
// factors (DST_ALPHA...) do it. The GS's integer steps (>> 7, >> 8) are floored as it truncates. The
// destination alpha test is the stencil test (gsn_stencil.frag copies alpha bit 7 into stencil first).
#extension GL_GOOGLE_include_directive : require
#include "gsn_common.glsl"

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) in vec4 inColour;
layout(location = 1) in vec3 inStq;
layout(location = 2) in vec2 inUv;
layout(location = 3) in float inFog;

layout(location = 0, index = 0) out vec4 outColour;
layout(location = 0, index = 1) out vec4 outFactor;

vec4 texel(vec2 coordinate) {
    // coordinate: normalised over the texture (2^TW x 2^TH).
    if ((draw.flags & kFromTarget) != 0u) {
        const vec2 gsPixel = draw.sourceOffset + coordinate * draw.textureSize;
        const vec4 t = texture(source, gsPixel / draw.sourceSize);
        vec4 c = vec4(t.rgb * 255.0, t.a * 128.0);
        if ((draw.flags & kTarget24) != 0u) {
            const bool black = all(lessThan(c.rgb, vec3(0.5)));
            c.a = ((draw.tex >> 24) & 1u) != 0u && black ? 0.0 : float((draw.tex >> 8) & 0xFFu);
        }
        return floor(c + 0.5);
    }
    return floor(texture(source, coordinate) * 255.0 + 0.5);
}

bool alphaPasses(float a) {
    const uint atst = (draw.alphaTest >> 1) & 7u;
    const float aref = float((draw.alphaTest >> 4) & 0xFFu);
    switch (atst) {
    case 0u: return false;
    case 1u: return true;
    case 2u: return a < aref;
    case 3u: return a <= aref;
    case 4u: return a == aref;
    case 5u: return a >= aref;
    case 6u: return a > aref;
    default: return a != aref;
    }
}

void main() {
    const vec4 cv = floor(inColour * 255.0 + 0.5);
    vec4 c = cv;
    if ((draw.flags & kTextured) != 0u) {
        const vec2 coordinate = (draw.flags & kUv) != 0u ? inUv / draw.textureSize : inStq.xy / inStq.z;
        const vec4 ct = texel(coordinate);
        const uint tfx = draw.tex & 3u;
        const bool tcc = (draw.tex & 4u) != 0u;
        switch (tfx) {
        case 0u:   // MODULATE
            c.rgb = floor(ct.rgb * cv.rgb / 128.0);
            c.a = tcc ? floor(ct.a * cv.a / 128.0) : cv.a;
            break;
        case 1u:   // DECAL
            c.rgb = ct.rgb;
            c.a = tcc ? ct.a : cv.a;
            break;
        case 2u:   // HIGHLIGHT
            c.rgb = floor(ct.rgb * cv.rgb / 128.0) + cv.a;
            c.a = tcc ? ct.a + cv.a : cv.a;
            break;
        default:   // HIGHLIGHT2
            c.rgb = floor(ct.rgb * cv.rgb / 128.0) + cv.a;
            c.a = tcc ? ct.a : cv.a;
            break;
        }
        c = min(c, vec4(255.0));
    }
    if ((draw.flags & kFog) != 0u) {
        const vec3 fog = vec3(draw.fogColour & 0xFFu, (draw.fogColour >> 8) & 0xFFu, (draw.fogColour >> 16) & 0xFFu);
        const float f = inFog;
        c.rgb = floor((f * c.rgb + (255.0 - f) * fog) / 256.0);
    }
    if ((draw.alphaTest & 1u) != 0u && !alphaPasses(c.a)) discard;   // AFAIL KEEP (the other modes: TODO)

    float written = c.a;
    if ((draw.flags & kFba) != 0u) written = mod(written, 128.0) + 128.0;
    // Source alpha for the blend: As (0x80 = 1.0); FIX likewise.
    const uint A = draw.blend & 3u, B = (draw.blend >> 2) & 3u, C = (draw.blend >> 4) & 3u, D = (draw.blend >> 6) & 3u;
    const float fix = float((draw.blend >> 8) & 0xFFu);
    vec3 s = vec3(1.0), d = vec3(0.0);
    if ((draw.flags & kBlend) != 0u && !((draw.flags & kPabe) != 0u && c.a < 128.0)) {
        if ((draw.flags & kDestAlphaBlend) != 0u) {
            s = vec3(1.0);   // the pipeline's factors (DST_ALPHA, ...) apply to Cs and Cd
        } else {
            const float k = (C == 0u ? c.a : fix) / 128.0;
            // (A - B) * k + D over Cs and Cd: A, B, D are 0 = Cs, 1 = Cd, 2 = 0.
            const float sk = (A == 0u ? k : 0.0) - (B == 0u ? k : 0.0) + (D == 0u ? 1.0 : 0.0);
            const float dk = (A == 1u ? k : 0.0) - (B == 1u ? k : 0.0) + (D == 1u ? 1.0 : 0.0);
            s = vec3(sk);
            d = vec3(dk);
        }
    }
    outColour = vec4(c.rgb / 255.0 * s, written / 128.0);
    outFactor = vec4(d, 0.0);
}
