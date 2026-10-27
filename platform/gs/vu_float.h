#pragma once
// The VU's float arithmetic, exactly: no denormals, infinities or NaNs (inputs flush to zero, results
// saturate to +-max), products and sums truncated toward zero. Shared by the reference interpreter
// (vu1_ref.cpp) and the native microprograms (vu1_native.cpp), which compute with VuF so that they give
// the interpreter's results bit for bit.
//
// The integer algorithms come from ps2-web-recomp (include/ps2/fpu.hpp), MIT licence, Copyright (c) 2026
// Samir Nuri; see THIRD_PARTY_NOTICES.md.
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace gt4::ps2::vufp {

constexpr uint32_t kSign = 0x80000000u, kAbs = 0x7FFFFFFFu, kOne = 0x3F800000u;

inline uint32_t exponent(uint32_t x) { return (x >> 23) & 0xFFu; }
inline uint32_t flush(uint32_t x) { return exponent(x) ? x : (x & kSign); }
inline uint32_t mantissa(uint32_t x) { return (x & 0x7FFFFFu) | 0x800000u; }

inline uint32_t pack(uint32_t sign, int e, uint32_t m) {
    if (e > 255) return sign | kAbs;
    if (e <= 0) return sign;
    return sign | (static_cast<uint32_t>(e) << 23) | (m & 0x7FFFFFu);
}

inline uint32_t add(uint32_t a, uint32_t b) {
    a = flush(a);
    b = flush(b);
    uint32_t aa = a & kAbs, ba = b & kAbs;
    if (!ba) return aa ? a : (a & b);
    if (!aa) return b;
    if (ba > aa) {
        std::swap(a, b);
        std::swap(aa, ba);
    }
    int e = static_cast<int>(aa >> 23);
    const int diff = e - static_cast<int>(ba >> 23);
    uint32_t ma = mantissa(aa) << 1;
    const uint32_t mb = diff > 25 ? 0u : (mantissa(ba) << 1) >> diff;
    if ((a ^ b) & kSign) {
        ma -= mb;
        if (!ma) return 0;
        const int shift = std::countl_zero(ma) - 7;
        ma <<= shift;
        e -= shift;
    } else {
        ma += mb;
        if (ma & 0x2000000u) {
            ma >>= 1;
            ++e;
        }
    }
    return pack(a & kSign, e, ma >> 1);
}

inline uint32_t sub(uint32_t a, uint32_t b) { return add(a, b ^ kSign); }

inline uint32_t mul(uint32_t a, uint32_t b) {
    const uint32_t sign = (a ^ b) & kSign;
    const int ea = static_cast<int>(exponent(a)), eb = static_cast<int>(exponent(b));
    if (!ea || !eb) return sign;
    uint64_t m = static_cast<uint64_t>(mantissa(a)) * mantissa(b);
    int e = ea + eb - 127;
    if (m >> 47) {
        m >>= 24;
        ++e;
    } else {
        m >>= 23;
    }
    return pack(sign, e, static_cast<uint32_t>(m));
}

inline double to_double(uint32_t x) {
    if (!exponent(x)) return (x & kSign) ? -0.0 : 0.0;
    const uint64_t bits = (static_cast<uint64_t>(x & kSign) << 32) | (static_cast<uint64_t>(exponent(x) + 1023 - 127) << 52) |
                          (static_cast<uint64_t>(x & 0x7FFFFFu) << 29);
    return std::bit_cast<double>(bits);
}

inline uint32_t from_double(double d) {
    const uint64_t bits = std::bit_cast<uint64_t>(d);
    const uint32_t sign = static_cast<uint32_t>(bits >> 32) & kSign;
    const int e = static_cast<int>((bits >> 52) & 0x7FFu);
    if (!e) return sign;
    return pack(sign, e - 1023 + 127, static_cast<uint32_t>(bits >> 29));
}

struct Result {
    uint32_t value;
    bool invalid;
};

inline Result div(uint32_t a, uint32_t b) {
    a = flush(a);
    b = flush(b);
    const uint32_t sign = (a ^ b) & kSign;
    if (!(b & kAbs)) return {sign | kAbs, true};
    if (!(a & kAbs)) return {sign, false};
    return {from_double(to_double(a) / to_double(b)), false};
}

inline uint32_t sqrt(uint32_t a) {
    a = flush(a) & kAbs;
    if (!a) return 0;
    return from_double(std::sqrt(to_double(a)));
}

inline Result rsqrt(uint32_t a, uint32_t b) {
    a = flush(a);
    b = flush(b) & kAbs;
    if (!b) return {(a & kSign) | kAbs, true};
    if (!(a & kAbs)) return {a & kSign, false};
    return {from_double(to_double(a) / std::sqrt(to_double(b))), false};
}

inline int32_t key(uint32_t x) {
    x = flush(x);
    const auto magnitude = static_cast<int32_t>(x & kAbs);
    return (x & kSign) ? -magnitude : magnitude;
}

inline uint32_t max(uint32_t a, uint32_t b) { return key(a) >= key(b) ? a : b; }
inline uint32_t min(uint32_t a, uint32_t b) { return key(a) <= key(b) ? a : b; }

inline uint32_t to_fixed(uint32_t x, int fraction) {
    const int e = static_cast<int>(exponent(x)) + fraction;
    if (e <= 126 || !exponent(x)) return 0;
    const bool negative = x & kSign;
    if (e >= 158) return negative ? 0x80000000u : 0x7FFFFFFFu;
    const int shift = e - 150;
    const uint32_t m = mantissa(x);
    const uint32_t v = shift >= 0 ? m << shift : m >> -shift;
    return negative ? 0u - v : v;
}

inline uint32_t from_fixed(int32_t v, int fraction) { return from_double(static_cast<double>(v) / static_cast<double>(1u << fraction)); }

}  // namespace gt4::ps2::vufp

namespace gt4::ps2 {

// A VU float: the operators compute as the VU's FMAC does. A MULA/MADDA/.../MADD chain is written as the
// left-to-right sum it computes, (a * b + c * d) + e * f, each product and each sum truncated.
struct VuF {
    uint32_t u = 0;
    static VuF bits(uint32_t value) { return VuF{value}; }
    static VuF of(float f) { return VuF{std::bit_cast<uint32_t>(f)}; }
    float f() const {   // as a host float (no NaN or infinity can come out of the VU's arithmetic)
        uint32_t v = u;
        if (!(v & 0x7F800000u)) v &= 0x80000000u;
        if ((v & 0x7F800000u) == 0x7F800000u) v = (v & 0x80000000u) | 0x7F7FFFFFu;
        return std::bit_cast<float>(v);
    }
};
inline VuF operator+(VuF a, VuF b) { return {vufp::add(a.u, b.u)}; }
inline VuF operator-(VuF a, VuF b) { return {vufp::sub(a.u, b.u)}; }
inline VuF operator*(VuF a, VuF b) { return {vufp::mul(a.u, b.u)}; }
inline VuF vmin(VuF a, VuF b) { return {vufp::min(a.u, b.u)}; }
inline VuF vmax(VuF a, VuF b) { return {vufp::max(a.u, b.u)}; }
inline VuF vdiv(VuF a, VuF b) { return {vufp::div(a.u, b.u).value}; }
inline uint32_t ftoi4(VuF a) { return vufp::to_fixed(a.u, 4); }
inline uint32_t ftoi0(VuF a) { return vufp::to_fixed(a.u, 0); }
constexpr VuF kVuOne{vufp::kOne};

// Four lanes, as stored in a VU data memory quadword.
struct VuVec {
    VuF x, y, z, w;
    VuF& operator[](int i) { return (&x)[i]; }
    const VuF& operator[](int i) const { return (&x)[i]; }
};
inline VuVec operator+(const VuVec& a, const VuVec& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline VuVec operator-(const VuVec& a, const VuVec& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
inline VuVec operator*(const VuVec& a, const VuVec& b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
inline VuVec operator*(const VuVec& a, VuF s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline VuVec vmin(const VuVec& a, const VuVec& b) { return {vmin(a.x, b.x), vmin(a.y, b.y), vmin(a.z, b.z), vmin(a.w, b.w)}; }
inline VuVec vmax(const VuVec& a, const VuVec& b) { return {vmax(a.x, b.x), vmax(a.y, b.y), vmax(a.z, b.z), vmax(a.w, b.w)}; }

// MULAx/MADDAy/MADDAz/MADDw: c0 * v.x + c1 * v.y + c2 * v.z + c3 * v.w, the VU's 4x4 transform.
inline VuVec transform(const VuVec c[4], const VuVec& v) {
    VuVec r;
    for (int i = 0; i < 4; ++i) r[i] = ((c[0][i] * v.x + c[1][i] * v.y) + c[2][i] * v.z) + c[3][i] * v.w;
    return r;
}

}  // namespace gt4::ps2
