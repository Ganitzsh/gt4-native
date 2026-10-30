// Native code for the hand-written assembly of decomp unit 00494578 (the graphics library at
// 0x494000-0x4B8000): its matrix helpers, which keep the current matrix in shared vectors 28..31
// (VU0 vf28-vf31 in the original), and a seed collector. Written from the original instructions
// (gt4-decomp: tools/match.py asm ADDR), including the VU0 microprograms the sine and the
// rotations start (0x58, 0x78, 0xB8, 0xF8); single precision in the same order (the VU's FMAC
// rounds the product, then the sum). The shared vectors 1..12 some helpers leave as the original
// leaves its registers are written too (callers read vector 1; 8..12 are shared state); the
// accumulator and Q are locals.
#include "guest.h"
#include "shared_state.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace gt4::rt;

namespace {

float (*const vf)[4] = gt4_shared_vectors;
const float kVf0[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // the original's constant register vf0

// The scratchpad's "matrix changed" byte (spr_base() + 0): the helpers set it after every change.
inline void mark_matrix_dirty() { *static_cast<volatile uint8_t *>(guest_ptr(0x70002000)) = 1; }

inline float sext_word(float x) {
    // the high word of a GPR after mfc1: the float's sign extended
    uint32_t u;
    std::memcpy(&u, &x, 4);
    const uint32_t s = (u & 0x80000000u) ? 0xFFFFFFFFu : 0;
    float f;
    std::memcpy(&f, &s, 4);
    return f;
}

inline float fbits(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
inline uint32_t ubits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// cvt.w.s on the EE: toward zero, saturating.
inline int32_t cvt_w(float x) {
    if (x >= 2147483648.0f) return 0x7FFFFFFF;
    if (x <= -2147483648.0f) return static_cast<int32_t>(0x80000000u);
    if (x != x) return 0;
    return static_cast<int32_t>(x);
}

// The EE has no infinities: a division by zero gives the largest float of the quotient's sign.
inline float ee_div(float a, float b) {
    if (b == 0.0f) return fbits(((ubits(a) ^ ubits(b)) & 0x80000000u) | 0x7F7FFFFFu);
    return a / b;
}

// The 4x4 product the helpers share (004A7698 from 004A76B4): column v of the argument matrix
// through vf28-vf31, ACC = vf28 * v.x + vf29 * v.y + vf30 * v.z, out = ACC + vf31 * v.w.
inline void through_current(const float *v, float *out) {
    float r[4];
    for (int k = 0; k < 4; ++k) {
        float acc = vf[28][k] * v[0];
        acc = acc + vf[29][k] * v[1];
        acc = acc + vf[30][k] * v[2];
        r[k] = acc + vf[31][k] * v[3];
    }
    std::memcpy(out, r, 16);
}

// 004A76B4: vf28-vf31 = vf28-vf31 * (vf1 vf2 vf3 vf4) (columns), with vf1-vf3 left holding the
// first three columns of the product.
void times_vf1_vf4() {
    through_current(vf[1], vf[1]);
    through_current(vf[2], vf[2]);
    through_current(vf[3], vf[3]);
    through_current(vf[4], vf[31]);
    mark_matrix_dirty();
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) vf[28 + r][k] = vf[1 + r][k] * kVf0[3];  // vmulw vf28+r, vf1+r, vf0w
}

// 004A7350: the angle (degrees) reduced for the sine polynomial: out[0] is the argument whose sine
// is sin(angle), out[1] the one whose sine is cos(angle), both in [-pi/2, pi/2]; out[2..3] get the
// high words of the GPRs (the signs of x and y, sign-extended), as pextlw leaves them in vf1.
void reduce_angle(float a, float out[4]) {
    const float k90 = fbits(0x42B40000u), k1_180 = fbits(0x3BB60B61u), kdeg = fbits(0x3C8EFA35u);
    const uint32_t kpi = 0x40490FDBu;
    float f13 = a - k90;
    float f2 = a * k1_180;
    float f3 = f13 * k1_180;
    const uint32_t v0 = (ubits(a) & 0x80000000u) ? 0xFFFFFFFFu : 0;  // mfc1; sra 31
    const uint32_t v1 = (ubits(f13) & 0x80000000u) ? 0xFFFFFFFFu : 0;
    float f12 = k90 - a;
    f13 = k90 - f13;
    const int32_t n2 = cvt_w(f2), n3 = cvt_w(f3);
    f12 = f12 * kdeg;
    f13 = f13 * kdeg;
    f12 = f12 - fbits(v0 & kpi);
    f13 = f13 - fbits(v1 & kpi);
    const float f4 = static_cast<float>(n2) * fbits(kpi);
    const float f5 = static_cast<float>(n3) * fbits(kpi);
    const uint32_t s0 = ((static_cast<uint32_t>(n2) + v0) & 1) << 31 | 0x3F800000u;
    const uint32_t s1 = ((static_cast<uint32_t>(n3) + v1) & 1) << 31 | 0x3F800000u;
    f12 = f12 + f4;
    f13 = f13 + f5;
    f12 = f12 * fbits(s0);
    f13 = f13 * fbits(s1);
    out[0] = f13;  // pextlw t0, t0(f12), t1(f13); qmtc2.i t0, vf1
    out[1] = f12;
    out[2] = sext_word(f13);
    out[3] = sext_word(f12);
}

// The sine of the VU0 micro image's subroutine at 0x10, which programs 0x58, 0x78, 0xB8 and 0xF8
// call: x + c3 x^3 + c5 x^5 + c7 x^7 + c9 x^9 for x in [-pi/2, pi/2], summed in its order.
float sine(float x) {
    const float x2 = x * x, x4 = x2 * x2, x3 = x2 * x, x5 = x4 * x, x7 = x4 * x3, x9 = x4 * x5;
    float acc = x3 * fbits(0xBE2AAAA4u);  // -1/6
    acc = acc + x5 * fbits(0x3C08873Eu);
    acc = acc + x7 * fbits(0xB94FB21Fu);
    acc = acc + x;
    return acc + x9 * fbits(0x362E9C14u);
}

// sin and cos of an angle in degrees (004A7438: the reduction, then microprogram 0x58).
struct SinCos {
    float s, c;
};
SinCos sin_cos_degrees(float a) {
    float args[4];
    reduce_angle(a, args);
    return {sine(args[0]), sine(args[1])};
}

// The rotations of microprograms 0x78, 0xB8, 0xF8: rows a and b of the current matrix turned by
// the angle: a' = a cos + b sin, b' = b cos - a sin (all four lanes).
void rotate_rows(int a, int b, float angle) {
    const SinCos t = sin_cos_degrees(angle);
    float ra[4], rb[4];
    for (int k = 0; k < 4; ++k) {
        ra[k] = vf[a][k] * t.c + vf[b][k] * t.s;
        rb[k] = vf[b][k] * t.c - vf[a][k] * t.s;
    }
    std::memcpy(vf[a], ra, 16);
    std::memcpy(vf[b], rb, 16);
}

// 004AC2DC: the sine polynomial of 004AC260/004AC334 (coefficients at 006B0060), for f in
// [-0.5, 0.5] quarter turns: pi/2 f + c.y f^3 + c.x f^5 + c.z f^7 + c.w f^9.
float sine_poly(float f) {
    float *vf1 = vf[1], *vf2 = vf[2], *vf3 = vf[3], *vf4 = vf[4], acc[4];
    for (int k = 0; k < 4; ++k) vf1[k] = vf2[k] = f;
    vf3[0] = fbits(0x3FC90FDBu);  // qmtc2 t1 (pi/2), its high word 0; z and w are not read
    vf3[1] = 0.0f;
    vf1[2] = vf1[2] * vf1[2];  // vmul.zw vf1, vf1, vf1
    vf1[3] = vf1[3] * vf1[3];
    for (int k = 0; k < 4; ++k) acc[k] = vf2[k] * vf3[0];  // vmulax ACC, vf2, vf3x
    std::memcpy(vf4, guest_ptr(0x006B0060), 16);
    vf1[0] = vf1[0] * vf1[3];  // vmulw.xz vf1, vf1, vf1w
    vf1[2] = vf1[2] * vf1[3];
    for (int k = 0; k < 4; ++k) vf2[k] = vf1[k] * vf1[2];  // vmulz vf2, vf1, vf1z
    for (int k = 0; k < 4; ++k) acc[k] = acc[k] + vf1[k] * vf4[1];  // vmadday ACC, vf1, vf4y
    for (int k = 0; k < 4; ++k) vf3[k] = vf2[k] * vf1[3];  // vmulw vf3, vf2, vf1w
    for (int k = 0; k < 4; ++k) acc[k] = acc[k] + vf4[k] * vf2[1];  // vmadday ACC, vf4, vf2y
    for (int k = 0; k < 4; ++k) acc[k] = acc[k] + vf2[k] * vf4[2];  // vmaddaz ACC, vf2, vf4z
    for (int k = 0; k < 4; ++k) vf1[k] = acc[k] + vf3[k] * vf4[3];  // vmaddw vf1, vf3, vf4w
    return vf1[0];
}

// 004AC3BC: the cosine polynomial (coefficients at 006B0070): 1 + d.y f^2 + d.z f^4 + d.x f^6 +
// d.w f^8.
float cosine_poly(float f) {
    float *vf1 = vf[1], *vf4 = vf[4], acc[4];
    const float *vf0 = kVf0;
    for (int k = 0; k < 4; ++k) vf1[k] = f;
    for (int k = 0; k < 4; ++k) vf1[k] = vf1[k] * vf1[k];  // vmul vf1, vf1, vf1
    std::memcpy(vf4, guest_ptr(0x006B0070), 16);
    for (int k = 0; k < 4; ++k) acc[k] = vf0[k] + vf0[3];  // vaddaw ACC, vf0, vf0w
    for (int k = 0; k < 4; ++k) acc[k] = acc[k] + vf1[k] * vf4[1];  // vmadday ACC, vf1, vf4y
    vf1[0] = vf1[0] * vf1[0];  // vmulx.x vf1, vf1, vf1x
    for (int k = 0; k < 4; ++k) acc[k] = acc[k] + vf1[k] * vf4[2];  // vmaddaz ACC, vf1, vf4z
    const float x = vf1[0];
    vf1[0] = vf1[0] * x;  // vmulx.xy vf1, vf1, vf1x
    vf1[1] = vf1[1] * x;
    for (int k = 0; k < 4; ++k) acc[k] = acc[k] + vf4[k] * vf1[1];  // vmadday ACC, vf4, vf1y
    for (int k = 0; k < 4; ++k) vf1[k] = acc[k] + vf1[k] * vf4[3];  // vmaddw vf1, vf1, vf4w
    return vf1[0];
}

// 004AC260 / 004AC334: the shared range reduction to quarter turns of (a + pi/4), then the sine or
// cosine polynomial and a sign flip by xor (t7).
float sin_cos(float a, bool cos_entry) {
    float f7 = a + fbits(0x3F490FDBu);           // + pi/4
    float f4 = f7 * fbits(0x3F22F983u);          // * 2/pi
    const uint32_t sign = ubits(f7) & 0x80000000u;
    const int32_t t8 = sign ? 1 : 0;
    const float half = fbits(0x3F000000u ^ sign);  // +-0.5
    const int32_t n = cvt_w(f4);
    f4 = f4 - static_cast<float>(n);
    const uint32_t t0 = static_cast<uint32_t>(n) - static_cast<uint32_t>(t8);
    f4 = f4 - half;
    uint32_t t7;
    bool sine;
    if (!cos_entry) {
        t7 = (t0 & 2) ? 0x80000000u : 0;
        sine = (t0 & 1) == 0;
    } else {
        // As the original: t7 starts as t0 + 1 and only becomes the sign bit when bit 1 is set, so
        // otherwise the result is xored with t0 + 1 itself (its low bits; usually 1: one ulp).
        t7 = t0 + 1;
        if (t7 & 2) t7 = 0x80000000u;
        sine = (t0 & 1) != 0;
    }
    const float r = sine ? sine_poly(f4) : cosine_poly(f4);
    return fbits(ubits(r) ^ t7);
}

}  // namespace

extern "C" {

// 004A7454 (40 bytes): vf28-vf31 = the identity matrix; marks the matrix changed.
void gt4_replace_func_004A7454(void) {
    const float *v0 = kVf0;
    for (int k = 0; k < 4; ++k) {
        vf[29][k] = v0[k] - v0[k];  // vsub.xyzw vf29, vf0, vf0
        vf[28][k] = v0[k] - v0[k];
        vf[31][k] = v0[k];          // vmove
    }
    const float r[4] = {v0[1], v0[2], v0[3], v0[0]};  // vmr32.xyzw vf30, vf0
    std::memcpy(vf[30], r, sizeof r);
    vf[29][1] = v0[1] + v0[3];  // vaddw.y vf29, vf0, vf0w
    mark_matrix_dirty();
    vf[28][0] = v0[0] + v0[3];  // vaddw.x vf28, vf0, vf0w
}

// 004A7844 (60 bytes): translate: vf31 = vf28 * x + vf29 * y + vf30 * z + vf31 (vf1-vf3 get x, y, z
// as qmtc2 leaves them); marks the matrix changed.
void gt4_replace_func_004A7844(float x, float y, float z) {
    vf[1][0] = x;
    vf[1][1] = sext_word(x);
    vf[2][0] = y;
    vf[2][1] = sext_word(y);
    vf[3][0] = z;
    vf[3][1] = sext_word(z);
    float acc[4];
    for (int k = 0; k < 4; ++k) {
        acc[k] = vf[28][k] * x;
        acc[k] = acc[k] + vf[29][k] * y;
        acc[k] = acc[k] + vf[30][k] * z;
    }
    mark_matrix_dirty();
    for (int k = 0; k < 4; ++k) vf[31][k] = acc[k] + vf[31][k] * kVf0[3];
}

// 004A7A54 (84 bytes): inverts the rigid transform in vf28-vf31: the 3x3 rotation is transposed
// (w of the rows becomes 0) and the translation becomes -(R^T t).
void gt4_replace_func_004A7A54(void) {
    for (int k = 0; k < 3; ++k) vf[31][k] = kVf0[k] - vf[31][k];
    float m[3][4];
    for (int r = 0; r < 3; ++r) {
        m[r][0] = vf[28][r];
        m[r][1] = vf[29][r];
        m[r][2] = vf[30][r];
        m[r][3] = 0.0f;
    }
    std::memcpy(vf[28], m[0], 16);
    std::memcpy(vf[29], m[1], 16);
    std::memcpy(vf[30], m[2], 16);
    const float tx = vf[31][0], ty = vf[31][1], tz = vf[31][2];
    float acc[3];
    for (int k = 0; k < 3; ++k) {
        acc[k] = vf[28][k] * tx;
        acc[k] = acc[k] + vf[29][k] * ty;
    }
    mark_matrix_dirty();
    for (int k = 0; k < 3; ++k) vf[31][k] = acc[k] + vf[30][k] * tz;
}

// 004A7AA8 (112 bytes): dst = src transformed by vf28-vf31 (4-component, any alignment); vf1 keeps
// the result.
void gt4_replace_func_004A7AA8(void *dst, const void *src) {
    float v[4];
    std::memcpy(v, src, 16);
    through_current(v, vf[1]);
    std::memcpy(dst, vf[1], 16);
}

// 004A7350 (232 bytes): the angle reduction above (vf1 = the sine arguments of sin a and cos a,
// a in degrees). Called only by the hand-written 004A7438, 004A7988, 004A79B0 and 004A79D8.
void gt4_replace_func_004A7350(float a) { reduce_angle(a, vf[1]); }

// 004A7438 (28 bytes): vf1.x = sin a, vf1.y = cos a (a in degrees): the reduction, then VU0
// microprogram 0x58 (the sine of both). Callers read vf1 (004A5EB0 sin, 004A5ED8 cos, 004A5F00
// both, 0049F658, 004A75D8).
void gt4_replace_func_004A7438(float a) {
    const SinCos t = sin_cos_degrees(a);
    vf[1][0] = t.s;
    vf[1][1] = t.c;
}

// 004A747C (36 bytes): the 3x3 part of vf28-vf30 = the identity (w and vf31 untouched); marks the
// matrix changed.
void gt4_replace_func_004A747C(void) {
    const float *v0 = kVf0;
    for (int k = 0; k < 3; ++k) {
        vf[29][k] = v0[k] - v0[k];
        vf[28][k] = v0[k] - v0[k];
    }
    const float r[3] = {v0[1], v0[2], v0[3]};  // vmr32.xyz vf30, vf0
    std::memcpy(vf[30], r, sizeof r);
    vf[29][1] = vf[29][1] + v0[3];  // vaddw.y vf29, vf29, vf0w
    mark_matrix_dirty();
    vf[28][0] = vf[28][0] + v0[3];  // vaddw.x vf28, vf28, vf0w
}

// 004A74A0 (20 bytes): the translation vf31.xyz = 0; marks the matrix changed.
void gt4_replace_func_004A74A0(void) {
    for (int k = 0; k < 3; ++k) vf[31][k] = kVf0[k] - kVf0[k];
    mark_matrix_dirty();
}

// 004A74B4 (156 bytes): vf28-vf31 = the 4x4 matrix at p (any alignment); marks the matrix changed.
void gt4_replace_func_004A74B4(const void *p) {
    std::memcpy(vf[28], p, 64);
    mark_matrix_dirty();
}

// 004A7550 (136 bytes): stores vf28-vf31 to p (any alignment).
void gt4_replace_func_004A7550(void *p) { std::memcpy(p, vf[28], 64); }

// 004A75D8 (192 bytes, with 004A7698 from 004A76B4): multiplies vf28-vf31 by the rotation of
// `angle` degrees about the axis (x, y, z) (normalised here; Rodrigues' formula with the sine and
// cosine of 004A7438). Called by ModelSet2::_render (00455708: angle, x, y, z read in that order).
// Leaves vf1-vf12 as the original does (vf8-vf12: its outer products and cross-product rows).
void gt4_replace_func_004A75D8(float angle, float x, float y, float z) {
    gt4_replace_func_004A7438(angle);
    const float *vf0 = kVf0;
    const float v5[4] = {x, y, z, 0.0f};  // stored on the stack with a zero w, lqc2 vf5
    std::memcpy(vf[5], v5, 16);
    float q;
    for (int k = 0; k < 4; ++k) vf[2][k] = vf[5][k] * vf[5][k];
    for (int k = 0; k < 4; ++k) vf[6][k] = vf[1][k] * vf0[3];
    for (int k = 0; k < 4; ++k) vf[7][k] = vf[5][k] * vf[5][0];
    for (int k = 0; k < 4; ++k) vf[4][k] = vf0[k] * vf0[3];
    vf[2][0] = vf[2][0] + vf[2][1];
    vf[6][3] = vf0[3] - vf[6][1];  // 1 - cos
    for (int k = 0; k < 4; ++k) vf[8][k] = vf[5][k] * vf[5][1];
    {
        const float r[4] = {vf[4][1], vf[4][2], vf[4][3], vf[4][0]};
        std::memcpy(vf[3], r, 16);
    }
    vf[2][0] = vf[2][0] + vf[2][2];
    for (int k = 0; k < 4; ++k) vf[9][k] = vf[5][k] * vf[5][2];
    {  // vrsqrt Q, vf0w, vf2x
        const float d = std::sqrt(std::fabs(vf[2][0]));
        q = d == 0.0f ? fbits((ubits(vf0[3]) & 0x80000000u) | 0x7F7FFFFFu) : vf0[3] / d;
    }
    for (int k = 0; k < 4; ++k) vf[10][k] = vf[11][k] = vf[12][k] = vf0[k] - vf0[k];
    {
        const float r[4] = {vf[3][1], vf[3][2], vf[3][3], vf[3][0]};
        std::memcpy(vf[2], r, 16);
    }
    vf[10][1] = vf[10][1] + vf[5][2];
    vf[11][0] = vf[11][0] - vf[5][2];
    vf[12][0] = vf[12][0] + vf[5][1];
    {
        const float r[4] = {vf[2][1], vf[2][2], vf[2][3], vf[2][0]};
        std::memcpy(vf[1], r, 16);
    }
    vf[10][2] = vf[10][2] - vf[5][1];
    vf[11][2] = vf[11][2] + vf[5][0];
    vf[12][1] = vf[12][1] - vf[5][0];
    vf[6][0] = vf[6][0] * q;  // vwaitq
    vf[6][3] = vf[6][3] * q;
    for (int k = 0; k < 4; ++k) {
        vf[7][k] = vf[7][k] * q;
        vf[8][k] = vf[8][k] * q;
        vf[9][k] = vf[9][k] * q;
    }
    for (int r = 0; r < 3; ++r) {  // rows 1-3: identity * cos + v v^T (1 - cos) + [v]x sin
        float *row = vf[1 + r];
        const float *outer = vf[7 + r], *cross = vf[10 + r];
        float acc[4];
        for (int k = 0; k < 4; ++k) {
            acc[k] = row[k] * vf[6][1];
            acc[k] = acc[k] + outer[k] * vf[6][3];
        }
        for (int k = 0; k < 3; ++k) row[k] = acc[k] + cross[k] * vf[6][0];
    }
    times_vf1_vf4();
}

// 004A7698 (220 bytes): vf28-vf31 = vf28-vf31 * the 4x4 matrix at m (any alignment); marks the
// matrix changed.
void gt4_replace_func_004A7698(const void *m) {
    std::memcpy(vf[1], m, 64);  // vf1-vf4
    times_vf1_vf4();
}

// 004A7774 (208 bytes): vf28-vf31 = the 4x4 matrix at m (any alignment) * vf28-vf31; marks the
// matrix changed.
void gt4_replace_func_004A7774(const void *m) {
    std::memcpy(vf[1], m, 64);  // vf1-vf4
    for (int c = 28; c < 32; ++c) {
        float *col = vf[c];
        float r[4];
        for (int k = 0; k < 4; ++k) {
            float acc = vf[1][k] * col[0];
            acc = acc + vf[2][k] * col[1];
            acc = acc + vf[3][k] * col[2];
            r[k] = acc + vf[4][k] * col[3];
        }
        std::memcpy(col, r, 16);
    }
    mark_matrix_dirty();
}

// 004A7890 (56 bytes): scales the matrix: vf28 *= x, vf29 *= y, vf30 *= z (all four components);
// vf1-vf3 get x, y, z as qmtc2 leaves them; marks the matrix changed.
void gt4_replace_func_004A7890(float x, float y, float z) {
    vf[1][0] = x;
    vf[1][1] = sext_word(x);
    vf[2][0] = y;
    vf[2][1] = sext_word(y);
    vf[3][0] = z;
    vf[3][1] = sext_word(z);
    for (int k = 0; k < 4; ++k) vf[28][k] = vf[28][k] * x;
    for (int k = 0; k < 4; ++k) vf[29][k] = vf[29][k] * y;
    mark_matrix_dirty();
    for (int k = 0; k < 4; ++k) vf[30][k] = vf[30][k] * z;
}

// 004A7880 (16 bytes, falls through into 004A7890): scale(1, 1 / sy, 1). No caller in the image.
void gt4_replace_func_004A7880(float sy) { gt4_replace_func_004A7890(1.0f, ee_div(1.0f, sy), 1.0f); }

// 004A7988 / 004A79B0 / 004A79D8 (40 bytes each): rotate the matrix by `angle` degrees about x, y
// or z: the reduction of 004A7350, then VU0 microprogram 0x78 / 0xB8 / 0xF8 (the sine and cosine,
// then rows 29 and 30 / 30 and 28 / 28 and 29 turned); mark the matrix changed.
void gt4_replace_func_004A7988(float angle) {
    mark_matrix_dirty();
    rotate_rows(29, 30, angle);
}
void gt4_replace_func_004A79B0(float angle) {
    mark_matrix_dirty();
    rotate_rows(30, 28, angle);
}
void gt4_replace_func_004A79D8(float angle) {
    mark_matrix_dirty();
    rotate_rows(28, 29, angle);
}

// 004A7A00 (84 bytes): transposes vf28-vf31 (4x4); marks the matrix changed.
void gt4_replace_func_004A7A00(void) {
    float t[4][4];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) t[r][c] = vf[28 + c][r];
    std::memcpy(vf[28], t, 64);
    mark_matrix_dirty();
}

// 004AC260 (212 bytes) sin(a), 004AC334 (204 bytes) cos(a), a in radians: one routine with two
// entries (sin_cos above). Leave vf1-vf4 as the original does. Caller: 00285690. Game code takes
// float results in eax (tools/game_cc.py, -mno-fp-ret-in-387): the bits are returned.
uint32_t gt4_replace_func_004AC260(float a) { return float_result(sin_cos(a, false)); }
uint32_t gt4_replace_func_004AC334(float a) { return float_result(sin_cos(a, true)); }

// 00539688 (140 bytes): fills 14 words at p with a running sum of the CPU clock (Count, read
// first and last), p itself, the TLB Random register and the caller's other argument registers
// (a1-a3, t0-t7: whatever they held; no argument reaches them natively, so they add 0).
unsigned int __gt4_ee_mfc0(int reg);
void gt4_replace_func_00539688(uint32_t *p) {
    uint32_t at = __gt4_ee_mfc0(9);
    p[0] = at;
    at += static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    at ^= __gt4_ee_mfc0(1);
    for (int k = 1; k <= 12; ++k) p[k] = at;  // + a1 ... + t7
    at += __gt4_ee_mfc0(9);
    p[13] = at;
}

}  // extern "C"
