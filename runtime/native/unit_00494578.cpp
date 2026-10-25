// Native code for the hand-written assembly of decomp unit 00494578 (the graphics library at
// 0x494000-0x4B8000): its VU0 matrix helpers, which keep the current matrix in VU0 vf28-vf31, and
// a seed collector. Written from the original instructions (gt4-decomp: tools/match.py asm ADDR);
// VU0 arithmetic is single precision in the same order (the VU's FMAC rounds the product, then the
// sum).
#include "vu0.h"

#include "guest.h"

#include <cstdint>
#include <cstring>

using namespace gt4::rt;
using gt4::native::vu0;

namespace {

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

}  // namespace

extern "C" {

// 004A7454 (40 bytes): vf28-vf31 = the identity matrix; marks the matrix changed.
void gt4_replace_func_004A7454(void) {
    Gt4Vu0State &s = vu0();
    const float *v0 = s.vf[0];
    for (int k = 0; k < 4; ++k) {
        s.vf[29][k] = v0[k] - v0[k];  // vsub.xyzw vf29, vf0, vf0
        s.vf[28][k] = v0[k] - v0[k];
        s.vf[31][k] = v0[k];          // vmove
    }
    const float r[4] = {v0[1], v0[2], v0[3], v0[0]};  // vmr32.xyzw vf30, vf0
    std::memcpy(s.vf[30], r, sizeof r);
    s.vf[29][1] = v0[1] + v0[3];  // vaddw.y vf29, vf0, vf0w
    mark_matrix_dirty();
    s.vf[28][0] = v0[0] + v0[3];  // vaddw.x vf28, vf0, vf0w
}

// 004A7844 (60 bytes): translate: vf31 = vf28 * x + vf29 * y + vf30 * z + vf31 (vf1-vf3 get x, y, z
// as qmtc2 leaves them); marks the matrix changed.
void gt4_replace_func_004A7844(float x, float y, float z) {
    Gt4Vu0State &s = vu0();
    s.vf[1][0] = x;
    s.vf[1][1] = sext_word(x);
    s.vf[2][0] = y;
    s.vf[2][1] = sext_word(y);
    s.vf[3][0] = z;
    s.vf[3][1] = sext_word(z);
    for (int k = 0; k < 4; ++k) {
        float acc = s.vf[28][k] * x;
        acc = acc + s.vf[29][k] * y;
        acc = acc + s.vf[30][k] * z;
        s.acc[k] = acc;
    }
    mark_matrix_dirty();
    for (int k = 0; k < 4; ++k) s.vf[31][k] = s.acc[k] + s.vf[31][k] * s.vf[0][3];
}

// 004A7A54 (84 bytes): inverts the rigid transform in vf28-vf31: the 3x3 rotation is transposed
// (w of the rows becomes 0) and the translation becomes -(R^T t).
void gt4_replace_func_004A7A54(void) {
    Gt4Vu0State &s = vu0();
    for (int k = 0; k < 3; ++k) s.vf[31][k] = s.vf[0][k] - s.vf[31][k];
    float m[3][4];
    for (int r = 0; r < 3; ++r) {
        m[r][0] = s.vf[28][r];
        m[r][1] = s.vf[29][r];
        m[r][2] = s.vf[30][r];
        m[r][3] = 0.0f;
    }
    std::memcpy(s.vf[28], m[0], 16);
    std::memcpy(s.vf[29], m[1], 16);
    std::memcpy(s.vf[30], m[2], 16);
    const float tx = s.vf[31][0], ty = s.vf[31][1], tz = s.vf[31][2];
    for (int k = 0; k < 3; ++k) {
        float acc = s.vf[28][k] * tx;
        acc = acc + s.vf[29][k] * ty;
        s.acc[k] = acc;
    }
    mark_matrix_dirty();
    for (int k = 0; k < 3; ++k) s.vf[31][k] = s.acc[k] + s.vf[30][k] * tz;
}

// 004A7AA8 (112 bytes): dst = src transformed by vf28-vf31 (4-component, any alignment); vf1 keeps
// the result.
void gt4_replace_func_004A7AA8(void *dst, const void *src) {
    Gt4Vu0State &s = vu0();
    float v[4];
    std::memcpy(v, src, 16);
    std::memcpy(s.vf[1], v, 16);
    for (int k = 0; k < 4; ++k) {
        float acc = s.vf[28][k] * v[0];
        acc = acc + s.vf[29][k] * v[1];
        acc = acc + s.vf[30][k] * v[2];
        s.acc[k] = acc;
        s.vf[1][k] = acc + s.vf[31][k] * v[3];
    }
    std::memcpy(dst, s.vf[1], 16);
}

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
