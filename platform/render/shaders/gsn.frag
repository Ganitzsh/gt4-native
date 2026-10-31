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

layout(location = 0) noperspective in vec4 inColour;
layout(location = 1) noperspective in vec3 inStq;
layout(location = 2) noperspective in vec2 inUv;
layout(location = 3) noperspective in float inFog;
layout(location = 4) flat in vec4 inBounds;   // a sprite's texture rectangle; min > max for other primitives

layout(location = 0, index = 0) out vec4 outColour;
layout(location = 0, index = 1) out vec4 outFactor;

// One axis of a texel index under CLAMP's mode: REPEAT, CLAMP, REGION_CLAMP (MIN, MAX), REGION_REPEAT
// (MSK, FIX).
int wrap(int i, uint mode, int size) {
    const int lo = int((mode >> 2) & 0x3FFu), hi = int((mode >> 12) & 0x3FFu);
    // The region modes can point past the texture (the GS reads on in memory): kept inside the image, as
    // reads outside it are undefined (a device loss in GT mode's map).
    switch (mode & 3u) {
    case 0u: return i & (size - 1);
    case 1u: return clamp(i, 0, size - 1);
    case 2u: return clamp(clamp(i, lo, hi), 0, size - 1);
    default: return ((i & lo) | hi) & (size - 1);
    }
}

// A texel coordinate of a target (in GS texels) moved to that target's render pixels' grid. Render pixel j
// holds what the GS draws at the point j / scale (gsn.vert), and the GS reads that point back at u =
// j / scale + 0.5 (u in [k, k + 1) is the GS pixel k): u - 0.5 + 0.5 / scale puts it at that render pixel's
// centre. At 1x it is u itself.
vec2 fromTargetPosition(vec2 position) { return position - 0.5 + 0.5 / draw.sourceScale; }

// Point sampling as the GS: the texel under the coordinate (in texels; a small bias keeps exact integers
// from rounding down), wrapped. From a target at a higher scale, the render pixel within that texel.
vec4 nearestTexel(vec2 position) {
    const vec2 cell = floor(position + 1.0 / 64.0);
    const ivec2 size = ivec2(draw.textureSize);
    const ivec2 i = ivec2(wrap(int(cell.x), draw.clampU, size.x), wrap(int(cell.y), draw.clampV, size.y));
    if ((draw.flags & kSnap) != 0u) {   // a reinterpreted texture: this render pixel's sub-texel
        const ivec2 sub = ivec2(gl_FragCoord.xy) % ivec2(draw.scale);
        return texelFetch(source, min(i * ivec2(draw.scale) + sub, textureSize(source, 0) - 1), 0) * 255.0;
    }
    if ((draw.flags & kFromTarget) != 0u) {
        // The render pixel drawn at the GS point this texel coordinate stands for (fromTargetPosition): a
        // copy at any scale reads each render pixel once, half a pixel from the next instead of on the edge.
        const vec2 fine = fromTargetPosition(position) * draw.scale + 1.0 / 64.0;
        const vec2 gsCell = floor(fine / draw.scale);
        const ivec2 j = ivec2(wrap(int(gsCell.x), draw.clampU, size.x), wrap(int(gsCell.y), draw.clampV, size.y));
        const vec2 within = min(fine - gsCell * draw.scale, draw.scale - 0.001);
        const ivec2 at = ivec2(floor((draw.sourceOffset + vec2(j)) * draw.scale + within));
        const vec4 t = texelFetch(source, clamp(at, ivec2(0), textureSize(source, 0) - 1), 0);
        return min(vec4(t.rgb * 255.0, t.a * 128.0), 255.0);   // the GS clamps what blends write (COLCLAMP)
    }
    return texelFetch(source, min(i, textureSize(source, 0) - 1), 0) * 255.0;
}

// Filtering inside a sprite's texture rectangle: at a higher resolution the samples near its edges would
// otherwise mix in the texels around it (atlas neighbours, as dotted lines under GT4's menu logos).
vec2 insideSprite(vec2 coordinate) {
    if ((draw.flags & kNearest) != 0u || inBounds.x > inBounds.z || atNativeScale()) return coordinate;
    const vec2 toNormal = (draw.flags & kUv) != 0u ? 1.0 / draw.textureSize : vec2(1.0);
    const vec2 lo = inBounds.xy * toNormal + 0.5 / draw.textureSize, hi = inBounds.zw * toNormal - 0.5 / draw.textureSize;
    const vec2 middle = (lo + hi) * 0.5;
    return clamp(coordinate, min(lo, middle), max(hi, middle));
}

// Bilinear filtering as the GS: the four texels around (position - 0.5), each wrapped by CLAMP's modes
// (per axis, including the region modes), weighted by the fraction (4 bits at 1x, as the GS).
vec4 bilinearTexel(vec2 position) {
    const vec2 base = position - 0.5;
    const vec2 cell = floor(base);
    vec2 f = base - cell;
    if (atNativeScale()) f = floor(f * 16.0) / 16.0;
    const ivec2 size = ivec2(draw.textureSize);
    const int u0 = wrap(int(cell.x), draw.clampU, size.x), u1 = wrap(int(cell.x) + 1, draw.clampU, size.x);
    const int v0 = wrap(int(cell.y), draw.clampV, size.y), v1 = wrap(int(cell.y) + 1, draw.clampV, size.y);
    const vec4 top = mix(texelFetch(source, ivec2(u0, v0), 0), texelFetch(source, ivec2(u1, v0), 0), f.x);
    const vec4 bottom = mix(texelFetch(source, ivec2(u0, v1), 0), texelFetch(source, ivec2(u1, v1), 0), f.x);
    return mix(top, bottom, f.y) * 255.0;
}

// A reinterpreted texture drawn scaled with the bilinear filter: the GS's four texels, each the mean of the
// render-scale sub-texels the reinterpretation built for it (draw.scale per axis).
vec4 subTexelMean(int u, int v) {
    const ivec2 s = ivec2(draw.scale), last = textureSize(source, 0) - 1;
    vec4 sum = vec4(0.0);
    for (int y = 0; y < s.y; ++y)
        for (int x = 0; x < s.x; ++x) sum += texelFetch(source, min(ivec2(u, v) * s + ivec2(x, y), last), 0);
    return sum / float(s.x * s.y);
}

vec4 bilinearSubTexels(vec2 position) {
    const vec2 base = position - 0.5;
    const vec2 cell = floor(base);
    vec2 f = base - cell;
    if (atNativeScale()) f = floor(f * 16.0) / 16.0;
    const ivec2 size = ivec2(draw.textureSize);
    const int u0 = wrap(int(cell.x), draw.clampU, size.x), u1 = wrap(int(cell.x) + 1, draw.clampU, size.x);
    const int v0 = wrap(int(cell.y), draw.clampV, size.y), v1 = wrap(int(cell.y) + 1, draw.clampV, size.y);
    const vec4 top = mix(subTexelMean(u0, v0), subTexelMean(u1, v0), f.x);
    const vec4 bottom = mix(subTexelMean(u0, v1), subTexelMean(u1, v1), f.x);
    return mix(top, bottom, f.y) * 255.0;
}

float pixelQ = 1.0;   // this pixel's Q (the GS's mipmap level comes from it)

// The GS's level of detail for its own mipmaps: K, or (log2(1 / Q) << L) + K, then less by the render
// scale's log2 so that a higher resolution keeps the same texels per pixel.
float gsLevel() {
    const float k = float(int(((draw.lod >> 3) & 0xFFFu) << 20) >> 20) / 16.0;
    float lod = (draw.lod & 1u) != 0u ? k : log2(1.0 / max(abs(pixelQ), 1e-20)) * exp2(float((draw.lod >> 1) & 3u)) + k;
    lod -= 0.5 * log2(draw.scale.x * draw.scale.y);
    return clamp(lod, 0.0, float((draw.lod >> 16) & 7u));
}

vec4 texel(vec2 coordinate) {
    // coordinate: normalised over the texture (2^TW x 2^TH).
    coordinate = insideSprite(coordinate);
    if ((draw.flags & kReplaced) != 0u) {
        // A texture pack's image (any size) for the rectangle of the GS texture at sourceOffset, sourceSize texels
        // (the whole texture, or the region PCSX2 named it by): the hardware's filtering and mips. CLAMP's region
        // modes wrap at the GS texture's texels, the position within the texel kept (the gradients of the
        // unwrapped coordinate).
        vec2 p = coordinate * draw.textureSize;
        if ((draw.clampU & 3u) >= 2u || (draw.clampV & 3u) >= 2u) {
            const vec2 cell = floor(p);
            const ivec2 size = ivec2(draw.textureSize);
            p = vec2(wrap(int(cell.x), draw.clampU, size.x), wrap(int(cell.y), draw.clampV, size.y)) + (p - cell);
        }
        const vec2 at = (p - draw.sourceOffset) / draw.sourceSize, perTexel = draw.textureSize / draw.sourceSize;
        vec4 c = textureGrad(source, at, dFdx(coordinate) * perTexel, dFdy(coordinate) * perTexel);
        // The alpha test sees level 0's alpha, as with made levels: averaged levels make cut-outs solid.
        if ((draw.alphaTest & 1u) != 0u) c.a = textureLod(source, at, 0.0).a;
        return floor(c * 255.0 + 0.5);
    }
    if ((draw.flags & kSnapFiltered) != 0u) return floor(bilinearSubTexels(coordinate * draw.textureSize) + 0.5);
    if ((draw.flags & (kNearest | kFromTarget | kMipmapped | kSnap)) == 0u) return floor(bilinearTexel(coordinate * draw.textureSize) + 0.5);
    if ((draw.flags & kNearest) != 0u) {
        vec4 c = nearestTexel(coordinate * draw.textureSize);
        if ((draw.flags & (kFromTarget | kTarget24)) == (kFromTarget | kTarget24)) {
            const bool black = all(lessThan(c.rgb, vec3(0.5)));
            c.a = ((draw.tex >> 24) & 1u) != 0u && black ? 0.0 : float((draw.tex >> 8) & 0xFFu);
        }
        return floor(c + 0.5);
    }
    if ((draw.flags & kFromTarget) != 0u) {
        const vec2 gsPixel = draw.sourceOffset + fromTargetPosition(coordinate * draw.textureSize);
        const vec4 t = texture(source, gsPixel / draw.sourceSize);
        vec4 c = min(vec4(t.rgb * 255.0, t.a * 128.0), 255.0);   // COLCLAMP, as above
        if ((draw.flags & kTarget24) != 0u) {
            const bool black = all(lessThan(c.rgb, vec3(0.5)));
            c.a = ((draw.tex >> 24) & 1u) != 0u && black ? 0.0 : float((draw.tex >> 8) & 0xFFu);
        }
        return floor(c + 0.5);
    }
    vec4 c = (draw.flags & (kMipmapped | kGeneratedMips)) == kMipmapped ? textureLod(source, coordinate, gsLevel())
                                                                      : texture(source, coordinate);
    // Made levels with the alpha test: the alpha of level 0, as the GS (without mipmaps) tests it; the levels
    // would average cut-outs (fences, foliage) into solid texels.
    if ((draw.flags & kGeneratedMips) != 0u && (draw.alphaTest & 1u) != 0u) c.a = textureLod(source, coordinate, 0.0).a;
    return floor(c * 255.0 + 0.5);
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

// An interpolated value moved from this render pixel's centre to its GS pixel's sample point (exact for the
// affine sprites that reinterpretation effects draw).
vec3 atGsPoint(vec3 value) {
    if ((draw.flags & kSnap) == 0u || atNativeScale()) return value;
    const vec2 offset = floor(gl_FragCoord.xy / draw.scale) * draw.scale + 0.5 - gl_FragCoord.xy;
    return value + dFdx(value) * offset.x + dFdy(value) * offset.y;
}

void main() {
    const vec4 cv = floor(inColour * 255.0 + 0.5);
    vec4 c = cv;
    if ((draw.flags & kTextured) != 0u) {
        const vec3 stq = atGsPoint(inStq);
        pixelQ = (draw.flags & kUv) != 0u ? 1.0 : stq.z;
        const vec2 uv = atGsPoint(vec3(inUv, 0.0)).xy;
        const vec2 coordinate = (draw.flags & kUv) != 0u ? uv / draw.textureSize : stq.xy / stq.z;
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
    // The alpha test; AFAIL's pass of the failing pixels has it inverted (gsn_scene.cpp sets its masks).
    if ((draw.alphaTest & 1u) != 0u && alphaPasses(c.a) == ((draw.flags & kAlphaInvert) != 0u)) discard;

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
