// GT4's lighting chain bodies and the VU's number rules (docs/VU1_PROGRAMS.md; platform/gs/renderers3d.cpp), shared by
// gsn_models.comp (the GS path) and modern_parity.vert (B7): the same operations on the same values, so both give the
// same colours and, through gt4_transform.glsl, the same Z (RISKS R3). The includer defines, before including:
//   uint word(uint q, uint lane)   the job's batch word (qword q & 255, lane)
//   uvec4 wordsAt(uint q)          its four words
//   vec4 floatsAt(uint q)          its four words as the VU reads them (vuFloat)
// and the vuFloat it uses (below, so define word/wordsAt first and floatsAt after this file, or forward-declare it).
// Arithmetic is 'precise' (no contraction into fused multiply-adds), so a compute and a vertex stage, each compiled its
// own way, give the same bits; the VU has no fused multiply-add either.

const float kMax = 3.40282347e38;
const float kMinNormal = 1.17549435e-38;

float vuFloat(uint b) {
    if ((b & 0x7F800000u) == 0u) b &= 0x80000000u;
    else if ((b & 0x7F800000u) == 0x7F800000u) b = (b & 0x80000000u) | 0x7F7FFFFFu;
    return uintBitsToFloat(b);
}
vec4 floatsAt(uint q) { const uvec4 w = wordsAt(q); return vec4(vuFloat(w.x), vuFloat(w.y), vuFloat(w.z), vuFloat(w.w)); }
float sat(float f) { return isnan(f) ? 0.0 : clamp(f, -kMax, kMax); }
vec4 sat(vec4 v) { return vec4(sat(v.x), sat(v.y), sat(v.z), sat(v.w)); }
vec3 sat(vec3 v) { return vec3(sat(v.x), sat(v.y), sat(v.z)); }
bool negative(float x) { return (floatBitsToUint(x) >> 31) != 0u; }
float divide(float a, float b) {
    if (b == 0.0) return negative(a) != negative(b) ? -kMax : kMax;
    precise float q = a / b;
    return sat(q);
}
float rsqrtOf(float a, float b) {
    b = abs(b);
    if (b == 0.0) return negative(a) ? -kMax : kMax;
    precise float q = a / sqrt(b);
    return sat(q);
}
// FTOI0 / FTOI4: truncation toward zero, saturating at the 32-bit limits
uint fixedPoint(float v, float scale) {
    if (isnan(v)) return 0u;
    precise float m = v * scale;
    const float s = trunc(m);
    if (s >= 2147483647.0) return 0x7FFFFFFFu;
    if (s <= -2147483648.0) return 0x80000000u;
    return uint(int(s));
}
uvec4 toIntegers(vec4 v) { return uvec4(fixedPoint(v.x, 1.0), fixedPoint(v.y, 1.0), fixedPoint(v.z, 1.0), fixedPoint(v.w, 1.0)); }
float itof0(uint w) { return float(int(w)); }
float itof12(uint w) { return float(int(w)) / 4096.0; }
vec4 itof0(uvec4 w) { return vec4(itof0(w.x), itof0(w.y), itof0(w.z), itof0(w.w)); }
bool positiveFlag(float z) { return z >= kMinNormal; }
bool negativeFlag(float z) { return z <= -kMinNormal; }
float power(float x, uint squarings) {
    for (uint k = 0u; k < squarings; ++k) x = sat(x * x);
    return x;
}

vec4 byteNormal(uvec4 w, float keepW) { return vec4(itof0(w.x) / 127.0, itof0(w.y) / 127.0, itof0(w.z) / 127.0, keepW); }

// max(m0 n.x + m1 n.y + m2 n.z, 0) per lane
vec4 dots(vec4 m0, vec4 m1, vec4 m2, vec4 n) {
    precise vec4 r = m0 * n.x + m1 * n.y + m2 * n.z;
    return max(sat(r), vec4(0.0));
}

vec4 reflection(vec4 m0, vec4 m1, vec4 m2, vec3 v, vec3 n, float d) {
    precise vec3 r = v - 2.0 * n * d;
    precise vec3 m = m0.xyz * r.x + m1.xyz * r.y + m2.xyz * r.z;
    return vec4(sat(m), 0.0);
}
uvec4 reflectiveColour(uvec4 given, vec4 baseColour, vec4 rim, float t) {
    precise vec4 c = itof0(given) * (baseColour + rim * t);
    return toIntegers(sat(c));
}

// One vertex as the lighting chain sees it: its position, ST (the reflective bodies keep the normal there), colour words
// and normal; `next` the next vertex's normal in batch order (the N.L model's quirk, below).
struct LitVertex {
    vec4 position;
    vec4 st;
    uvec4 colour;
    vec4 normal;
};

// The lighting chain's body `body` on one vertex: its colour words (and for some bodies its ST) as the chain leaves them.
void lightVertex(uint dotModel, uint body, inout LitVertex v, vec4 next) {
    if (body == 0x0B0u) return;
    if (body == 0xCA8u || body == 0xEA0u) {   // specular from the eye
        const vec4 eye = floatsAt(204u), coefficient = floatsAt(205u);
        const vec3 light = vec3(floatsAt(201u).x, floatsAt(202u).x, floatsAt(203u).x);
        precise vec3 d = v.position.xyz - eye.xyz;
        const vec3 normal = v.normal.xyz;
        precise float twice = 2.0 * dot(d, normal);
        precise float length2 = dot(d, d);
        const float inverseLength = rsqrtOf(1.0, length2);
        precise vec3 reflected = d - normal * twice;
        precise float s;
        if (body == 0xCA8u) {
            precise vec3 scaled = reflected * inverseLength;
            const vec3 unit = sat(vec4(scaled, 0.0)).xyz;
            precise float l = dot(light, unit);
            s = power(max(sat(l), 0.0), 3u);
        } else {
            precise float l = dot(light, reflected) * inverseLength;
            s = power(max(sat(l), 0.0), 4u);
        }
        precise vec4 c = vec4(coefficient.x * s, coefficient.y * s, coefficient.z * s, 128.0);
        v.colour = toIntegers(c);
        return;
    }
    if (body == 0x10A8u || body == 0x12C8u) {   // reflective paint: a sphere map, or a map by quadrant
        const vec4 eye = floatsAt(204u), baseColour = floatsAt(205u), rim = floatsAt(206u);
        const vec4 m0 = floatsAt(201u), m1 = floatsAt(202u), m2 = floatsAt(203u);
        const vec3 normal = v.st.xyz;
        precise vec3 e3 = v.position.xyz - eye.xyz;
        precise float d = dot(e3, normal);
        precise float length2 = dot(e3, e3);
        const float e = rsqrtOf(1.0, length2);
        precise float rimT = min(d, 0.0) * e;
        v.colour = reflectiveColour(v.colour, baseColour, rim, sat(rimT));
        if (body == 0x10A8u) {
            precise vec4 r = reflection(m0, m1, m2, e3, normal, d) * e;
            precise vec4 s = sat(r);
            s.z = s.z + 1.0;
            precise float length2 = s.x * s.x + s.y * s.y + s.z * s.z;
            const float scaleBy = rsqrtOf(0.5, length2);
            precise float sx = s.x * scaleBy + 0.5, sy = 0.5 - s.y * scaleBy;
            v.st.x = sx;
            v.st.y = sy;
        } else {
            const vec4 s = reflection(m0, m1, m2, e3, normal, d);
            precise float difference = s.x - s.z, sum = s.x + s.z;
            const bool a = negative(difference), c = negative(sum);
            float numerator, denominator, quadrant;
            if (a == c) {
                numerator = s.z;
                denominator = s.x;
                quadrant = a ? 4.0 : 0.0;
            } else {
                numerator = -s.x;
                denominator = s.z;
                quadrant = a ? 2.0 : 6.0;
            }
            precise float uq = quadrant + divide(numerator, denominator);
            const float u = sat(uq);
            precise float te = e * s.y;
            const float t = sat(te);
            precise float cubic = t * t * t + (t + t);
            const float vv = sat(cubic);
            precise float sx = m1.w + m0.w * u, sy = eye.w + m2.w * vv;
            v.st.x = sat(sx);
            v.st.y = sat(sy);
        }
        return;
    }
    if (body == 0x1608u) {   // a planar projection
        const vec4 m0 = floatsAt(201u), m1 = floatsAt(202u), m2 = floatsAt(203u), offset = floatsAt(204u);
        const vec4 p = v.position;
        v.colour = toIntegers(floatsAt(205u));
        precise float sx = offset.x + m0.x * p.x + m1.x * p.y + m2.x * p.z;
        precise float sy = offset.y + m0.y * p.x + m1.y * p.y + m2.y * p.z;
        v.st.x = sat(sx);
        v.st.y = sat(sy);
        return;
    }
    if (body == 0x1508u) {   // prelit colours
        const vec4 scaleBy = floatsAt(201u), limit = floatsAt(202u), offset = floatsAt(203u);
        precise vec4 c = offset + itof0(v.colour) * scaleBy;
        v.colour = toIntegers(min(max(sat(c), vec4(0.0)), limit));
        return;
    }
    if (body == 0x16F8u) {   // fade (alpha 0 drew nothing: not sent here)
        const uint table = (word(202u, 0u) & 0xFFFFu) + 203u;
        precise float alpha = itof0(word(table & 0x3FFu, 0u)) * (1.0 / 255.0);
        const vec4 scaleBy = floatsAt(201u);
        const vec4 c = itof0(v.colour);
        precise vec4 scaled = vec4(c.x * scaleBy.x, c.y * scaleBy.y, c.z * scaleBy.z, c.w * alpha);
        v.colour = toIntegers(min(scaled, vec4(255.0)));
        return;
    }
    // the N.L model of the other bodies
    const uint squaringsA = dotModel & 15u, squaringsB = (dotModel >> 8) & 15u;
    const bool second = ((dotModel >> 4) & 1u) != 0u, nextNormal = ((dotModel >> 12) & 1u) != 0u;
    const vec4 a0 = floatsAt(201u), a1 = floatsAt(202u), a2 = floatsAt(203u);
    const vec4 ambient = floatsAt(204u);
    const vec4 limit = floatsAt(second ? 216u : 209u);
    const vec4 normal = v.normal;
    vec4 d = dots(a0, a1, a2, normal), f = vec4(0.0);
    d.z = power(d.z, squaringsA);
    d.w = power(d.w, squaringsA);
    if (second) {
        f = dots(floatsAt(209u), floatsAt(210u), floatsAt(211u), normal);
        f = vec4(power(f.x, squaringsB), power(f.y, squaringsB), power(f.z, squaringsB), power(f.w, squaringsB));
    }
    precise vec4 acc = nextNormal ? (a0 * next.x + a1 * next.y) + ambient : ambient;
    acc += floatsAt(205u) * d.x + floatsAt(206u) * d.y + floatsAt(207u) * d.z + floatsAt(208u) * d.w;
    if (second) acc += floatsAt(212u) * f.x + floatsAt(213u) * f.y + floatsAt(214u) * f.z + floatsAt(215u) * f.w;
    v.colour = toIntegers(min(max(sat(acc), vec4(0.0)), limit));
}
