// Shared helpers for the host post-processing shaders. Colours in the HDR pipeline are linear Rec.709
// scene-referred radiance; "LDR" images hold display-referred values already encoded with the sRGB curve.
#ifndef GT4_COMMON_GLSL
#define GT4_COMMON_GLSL

const float PI = 3.14159265358979;

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float max3(vec3 v) { return max(v.x, max(v.y, v.z)); }

// sRGB transfer functions (IEC 61966-2-1), exact piecewise form.
vec3 srgbEncode(vec3 linear) {
    linear = max(linear, vec3(0.0));
    vec3 low = linear * 12.92;
    vec3 high = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(linear, vec3(0.0031308)));
}
vec3 srgbDecode(vec3 encoded) {
    encoded = max(encoded, vec3(0.0));
    vec3 low = encoded / 12.92;
    vec3 high = pow((encoded + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(encoded, vec3(0.04045)));
}

// YCoCg, used for temporal neighbourhood clipping (Karis 2014, "High Quality Temporal Supersampling").
vec3 rgbToYCoCg(vec3 c) {
    return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}
vec3 yCoCgToRgb(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Reversible tonemap used to filter HDR samples without fireflies dominating (Karis 2013).
vec3 tonemapWeight(vec3 c) { return c / (1.0 + max3(c)); }
vec3 tonemapWeightInverse(vec3 c) { return c / max(1.0 - max3(c), 1e-5); }

// Interleaved gradient noise (Jimenez 2014, "Next Generation Post Processing in Call of Duty: AW").
float interleavedGradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Integer hash (Jarzynski & Olano 2020, "Hash Functions for GPU Rendering", PCG variant).
uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float hashToFloat(uint h) { return float(h >> 8) * (1.0 / 16777216.0); }

#endif
