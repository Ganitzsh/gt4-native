// The view space of the game's 3D scene, from its Z buffer (docs/NATIVE_RENDERER.md, "The scene stage"): GT4's
// 3D renderers write Z = zScale / view distance (24-bit Z; race captures). Metres; x right, y down, z ahead.
// `pixel` is a position in render pixels (a pixel's centre at + 0.5), `size` the scene's area in render pixels
// and `tanHalf` the tangents of half the field of view across and down.

float viewDistance(sampler2D depthBuffer, ivec2 q, float zScale) {
    const float z = texelFetch(depthBuffer, q, 0).r * 4294967296.0;
    return z > 0.5 ? zScale / z : 1e30;
}

vec3 viewPosition(vec2 pixel, float d, vec2 size, vec2 tanHalf) {
    const vec2 ndc = pixel / size * 2.0 - 1.0;
    return vec3(ndc * tanHalf * d, d);
}

vec2 viewToPixel(vec3 v, vec2 size, vec2 tanHalf) {
    return (v.xy / (v.z * tanHalf) * 0.5 + 0.5) * size;
}

// The surface's normal from the nearer neighbour on each axis (edges keep their own side), towards the camera.
vec3 viewNormal(sampler2D depthBuffer, ivec2 q, float d, vec2 size, vec2 tanHalf, float zScale) {
    const ivec2 last = ivec2(size) - 1;
    const ivec2 l = max(q - ivec2(1, 0), ivec2(0)), r = min(q + ivec2(1, 0), last);
    const ivec2 u = max(q - ivec2(0, 1), ivec2(0)), b = min(q + ivec2(0, 1), last);
    const float dl = viewDistance(depthBuffer, l, zScale), dr = viewDistance(depthBuffer, r, zScale);
    const float du = viewDistance(depthBuffer, u, zScale), db = viewDistance(depthBuffer, b, zScale);
    const vec3 P = viewPosition(vec2(q) + 0.5, d, size, tanHalf);
    const vec3 ex = abs(dr - d) < abs(dl - d) ? viewPosition(vec2(r) + 0.5, dr, size, tanHalf) - P : P - viewPosition(vec2(l) + 0.5, dl, size, tanHalf);
    const vec3 ey = abs(db - d) < abs(du - d) ? viewPosition(vec2(b) + 0.5, db, size, tanHalf) - P : P - viewPosition(vec2(u) + 0.5, du, size, tanHalf);
    vec3 n = normalize(cross(ex, ey));
    return dot(n, P) > 0.0 ? -n : n;
}

// Interleaved gradient noise (Jimenez 2014), 0..1.
float gradientNoise(vec2 q) { return fract(52.9829189 * fract(dot(q, vec2(0.06711056, 0.00583715)))); }

// The targets hold the game's colours / 255 (display-referred): effects mix in linear light.
vec3 toLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 toDisplay(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }
