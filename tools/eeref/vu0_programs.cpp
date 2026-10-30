// GT4's VU0 microprograms, written in C++ in the VU's arithmetic: a test reference (docs/NATIVE_ONLY.md, layer 1).
// The game runs none of this: its callers have plain C++ versions of what they need (runtime/native/
// unit_00494578.cpp: the sine and the rotations; compat/game/gt4_vec.h: the 2D projection; game_src/: the rest).
// The GT4_ORIG bridge (orig_bridge.cpp) runs the original code's vcallms here, and `eeref --vu0-fuzz` checks these
// against the interpreter (vu0_interp.cpp).
//
// The game uploads one VU0 micro image at boot (0xA80 bytes, 004A03C0) and starts its programs with vcallms
// at 14 entry addresses. Each function below does what one program does, operation for operation in the
// interpreter's arithmetic (operands and results through vu_float, MAX/MINI on the bits, FTOI toward zero), so
// the registers and data memory it leaves are the interpreter's bit for bit. Every register a program writes is
// written, temporaries included: callers read results with qmfc2 and some keep using the registers afterwards.
// tools/vu0_disasm.py prints the micro image these were written from (from the user's executable).
#include "vu0_programs.h"

#include <cmath>
#include <cstring>

namespace vu0ref {
namespace {

constexpr int X = 1, Y = 2, Z = 4, W = 8, XYZW = 15;
constexpr int ACC = -1;  // a destination: the accumulator

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
// as in vu0.cpp: no infinities, NaNs or denormals
inline float vu(float f) {
    const uint32_t u = ubits(f);
    if ((u & 0x7F800000u) == 0x7F800000u) return fbits((u & 0x80000000u) | 0x7F7FFFFFu);
    if ((u & 0x7F800000u) == 0) return fbits(u & 0x80000000u);
    return f;
}
inline int32_t vu_order(float f) {
    const int32_t i = static_cast<int32_t>(ubits(f));
    return i ^ ((i >> 31) & 0x7FFFFFFF);
}
inline float vu_div(float a, float b) {
    if (b == 0.0f) return fbits(((ubits(a) ^ ubits(b)) & 0x80000000u) | 0x7F7FFFFFu);
    return a / b;
}
inline int32_t ftoi(float x) {
    if (x >= 2147483648.0f) return 0x7FFFFFFF;
    if (x <= -2147483648.0f) return static_cast<int32_t>(0x80000000u);
    if (x != x) return 0;
    return static_cast<int32_t>(x);
}

// One program's view of the registers. Upper instructions take their operands before writing (a whole
// lane set computed, then stored), so a result may be one of its own operands.
struct Vu0 {
    Gt4Vu0State &s;
    unsigned char *data;  // VU0 data memory (4 KB)
    float *reg(int n) { return n == ACC ? s.acc : s.vf[n]; }
    void store(int d, const float *v, int mask) {
        if (d == 0) return;
        float *o = reg(d);
        for (int k = 0; k < 4; ++k)
            if (mask & (1 << k)) o[k] = v[k];
    }
    // d = fs OP t, t: ft's lanes (bc < 0), ft's lane bc, or the scalar (Q, I) given as tv
    enum Op { Add, Sub, Madd, Msub, Mul, Max, Mini };
    void op(Op o, int d, int fs, int ft, int bc, int mask, const float *scalar = nullptr) {
        const float *rs = s.vf[fs];
        const float *rt = ft >= 0 ? s.vf[ft] : nullptr;
        float r[4] = {};
        for (int k = 0; k < 4; ++k) {
            if (!(mask & (1 << k))) continue;
            const bool bits = o == Max || o == Mini;   // MAX / MINI: the operands' bits (Q and I already vu())
            const float rawb = scalar ? *scalar : bc >= 0 ? rt[bc] : rt[k];
            const float a = bits ? rs[k] : vu(rs[k]);
            const float b = bits ? rawb : vu(rawb);
            const float acc = vu(s.acc[k]);
            float v;
            switch (o) {
            case Add: v = a + b; break;
            case Sub: v = a - b; break;
            case Madd: v = acc + a * b; break;
            case Msub: v = acc - a * b; break;
            case Mul: v = a * b; break;
            case Max: v = vu_order(a) > vu_order(b) ? a : b; break;
            default: v = vu_order(a) < vu_order(b) ? a : b; break;
            }
            r[k] = bits ? v : vu(v);
        }
        store(d, r, mask);
    }
    // the forms the programs use
    void mul(int d, int fs, int ft, int mask = XYZW) { op(Mul, d, fs, ft, -1, mask); }
    void mul(int d, int fs, int ft, char bc, int mask = XYZW) { op(Mul, d, fs, ft, lane(bc), mask); }
    void madd(int d, int fs, int ft, int mask = XYZW) { op(Madd, d, fs, ft, -1, mask); }
    void madd(int d, int fs, int ft, char bc, int mask = XYZW) { op(Madd, d, fs, ft, lane(bc), mask); }
    void msub(int d, int fs, int ft, char bc, int mask = XYZW) { op(Msub, d, fs, ft, lane(bc), mask); }
    void add(int d, int fs, int ft, int mask = XYZW) { op(Add, d, fs, ft, -1, mask); }
    void add(int d, int fs, int ft, char bc, int mask = XYZW) { op(Add, d, fs, ft, lane(bc), mask); }
    void sub(int d, int fs, int ft, int mask = XYZW) { op(Sub, d, fs, ft, -1, mask); }
    void max(int d, int fs, int ft, char bc, int mask = XYZW) { op(Max, d, fs, ft, lane(bc), mask); }
    void mini(int d, int fs, int ft, int mask = XYZW) { op(Mini, d, fs, ft, -1, mask); }
    void mini(int d, int fs, int ft, char bc, int mask = XYZW) { op(Mini, d, fs, ft, lane(bc), mask); }
    void muli(int d, int fs, int mask = XYZW) { const float i = vu(s.i); op(Mul, d, fs, -1, -1, mask, &i); }
    void maddi(int d, int fs, int mask = XYZW) { const float i = vu(s.i); op(Madd, d, fs, -1, -1, mask, &i); }
    void addi(int d, int fs, int mask = XYZW) { const float i = vu(s.i); op(Add, d, fs, -1, -1, mask, &i); }
    void mulq(int d, int fs, int mask = XYZW) { const float q = vu(s.q); op(Mul, d, fs, -1, -1, mask, &q); }
    void addq(int d, int fs, int mask = XYZW) { const float q = vu(s.q); op(Add, d, fs, -1, -1, mask, &q); }
    static int lane(char c) { return c == 'x' ? 0 : c == 'y' ? 1 : c == 'z' ? 2 : 3; }

    void itof0(int ft, int fs) {
        float r[4];
        for (int k = 0; k < 4; ++k) r[k] = static_cast<float>(static_cast<int32_t>(ubits(s.vf[fs][k]))) / 1.0f;
        store(ft, r, XYZW);
    }
    void ftoi(int ft, int fs, float scale, int mask) {
        float r[4];
        for (int k = 0; k < 4; ++k) r[k] = fbits(static_cast<uint32_t>(vu0ref::ftoi(vu(s.vf[fs][k]) * scale)));
        store(ft, r, mask);
    }
    void move(int ft, int fs) {
        float r[4];
        std::memcpy(r, s.vf[fs], 16);
        store(ft, r, XYZW);
    }
    // DIV and RSQRT: every program waits for Q (WAITQ) before it reads it, so the result is Q from then on
    void div(int fs, char fsf, int ft, char ftf) { s.q = vu(vu_div(vu(s.vf[fs][lane(fsf)]), vu(s.vf[ft][lane(ftf)]))); }
    void rsqrt(int fs, char fsf, int ft, char ftf) {
        s.q = vu(vu_div(vu(s.vf[fs][lane(fsf)]), std::sqrt(std::fabs(vu(s.vf[ft][lane(ftf)])))));
    }
    float *mem(uint32_t qw) { return reinterpret_cast<float *>(data + ((qw & 0xFF) << 4)); }
    void set_vi(int n, uint32_t v) {
        if (n) s.vi[n] = v & 0xFFFF;
    }

    // 0x10: vf1 = sin(vf1), lane by lane: x + x^3 c3 + x^5 c5 + x^7 c7 + x^9 c9 (Taylor). The caller has put
    // x^2 in vf2 and x^4 in vf4 (in BAL's delay slot); vi15 is the return.
    void sine() {
        mul(3, 2, 1);                // x^3
        mul(2, 4, 1);                // x^5
        mul(5, 4, 3);                // x^7
        s.i = fbits(0xBE2AAAA4u);   // -1/6
        muli(ACC, 3);
        mul(4, 4, 2);                // x^9
        s.i = fbits(0x3C08873Eu);
        maddi(ACC, 2);
        s.i = fbits(0xB94FB21Fu);
        maddi(ACC, 5);
        s.i = fbits(0x362E9C14u);
        madd(ACC, 1, 0, 'w');        // + x
        maddi(1, 4);
    }
    // the start every sine caller shares: x^2 and BAL (vi15 = the return); the caller then does BAL's delay
    // slot, x^4 in vf4 (and for the rotations a copy of the row it overwrites first)
    void sine_call(uint32_t bal_pc) {
        mul(2, 1, 1);
        set_vi(15, (bal_pc + 16) / 8);
    }
};

// The programs, by entry address.

// 0x58: vf1 = sin(vf1)
void prog_0058(Vu0 &v) {
    v.sine_call(0x58);
    v.mul(4, 2, 2);
    v.sine();
}

// 0x78: rotation about X: vf29' = vf29 cos + vf30 sin, vf30' = vf30 cos - vf29 sin (vf1 = (sin, cos))
void prog_0078(Vu0 &v) {
    v.sine_call(0x78);
    v.mul(4, 2, 2);
    v.move(6, 29);
    v.sine();
    v.mul(ACC, 29, 1, 'y');
    v.madd(29, 30, 1, 'x');
    v.mul(ACC, 30, 1, 'y');
    v.msub(30, 6, 1, 'x');
}

// 0xB8: rotation about Y: vf30' = vf28 sin + vf30 cos, vf28' = vf28 cos - vf30 sin
void prog_00B8(Vu0 &v) {
    v.sine_call(0xB8);
    v.mul(4, 2, 2);
    v.move(6, 30);
    v.sine();
    v.mul(ACC, 28, 1, 'x');
    v.madd(30, 30, 1, 'y');
    v.mul(ACC, 28, 1, 'y');
    v.msub(28, 6, 1, 'x');
}

// 0xF8: rotation about Z: vf28' = vf28 cos + vf29 sin, vf29' = vf29 cos - vf28 sin
void prog_00F8(Vu0 &v) {
    v.sine_call(0xF8);
    v.mul(4, 2, 2);
    v.move(6, 28);
    v.sine();
    v.mul(ACC, 28, 1, 'y');
    v.madd(28, 29, 1, 'x');
    v.mul(ACC, 29, 1, 'y');
    v.msub(29, 6, 1, 'x');
}

// 0x308 (0x2E8 converts the integer weights, normal and position first): a vertex skinned by four bones.
// vf1 the weights, vf3 the normal, vf2 the position with vf4.x its scale; bone k's rows in vf(8+4k)..vf(10+4k),
// its translation in vf(11+4k). Out: vf2 the position; vf3 the normal, blended by the bones' rotations and
// normalised (times 1/sqrt of its squared length, RSQRT issued at 0x3E0 and waited for at 0x440).
void skin4_tail(Vu0 &v) {
    v.mul(5, 3, 1, 'x');
    v.mul(6, 3, 1, 'y');
    v.mul(7, 3, 1, 'z');
    v.mul(3, 3, 1, 'w');
    v.mul(ACC, 8, 5, 'x');
    v.madd(ACC, 9, 5, 'y');
    v.madd(ACC, 10, 5, 'z');
    v.madd(ACC, 12, 6, 'x');
    v.madd(ACC, 13, 6, 'y');
    v.madd(ACC, 14, 6, 'z');
    v.madd(ACC, 16, 7, 'x');
    v.madd(ACC, 17, 7, 'y');
    v.madd(ACC, 18, 7, 'z');
    v.madd(ACC, 20, 3, 'x');
    v.madd(ACC, 21, 3, 'y');
    v.madd(3, 22, 3, 'z');
    v.mul(2, 2, 4, 'x');
    v.mul(7, 3, 3);
    v.mul(6, 2, 1, 'x');
    v.mul(5, 2, 1, 'y');
    v.add(7, 7, 7, 'y', X);
    v.mul(ACC, 8, 6, 'x');
    v.madd(ACC, 9, 6, 'y');
    v.madd(ACC, 10, 6, 'z');
    v.add(7, 7, 7, 'z', X);
    v.madd(ACC, 11, 1, 'x');
    v.mul(6, 2, 1, 'z');
    v.madd(ACC, 12, 5, 'x');
    v.rsqrt(0, 'w', 7, 'x');
    v.madd(ACC, 13, 5, 'y');
    v.madd(ACC, 14, 5, 'z');
    v.madd(ACC, 15, 1, 'y');
    v.mul(2, 2, 1, 'w');
    v.madd(ACC, 16, 6, 'x');
    v.madd(ACC, 17, 6, 'y');
    v.madd(ACC, 18, 6, 'z');
    v.madd(ACC, 19, 1, 'z');
    v.madd(ACC, 20, 2, 'x');
    v.madd(ACC, 21, 2, 'y');
    v.madd(ACC, 22, 2, 'z');
    v.madd(2, 23, 1, 'w');
    v.mulq(3, 3);
}

void convert_weights(Vu0 &v, int second) {  // ITOF0 of vf1, vf3 and vf2 (0x2E8) or vf1 and vf2 (0x460), I = 1/127
    v.itof0(1, 1);
    v.s.i = fbits(0x3C010204u);
    if (second == 3) v.itof0(3, 3);
    v.itof0(2, 2);
    v.muli(1, 1);
}

void prog_02E8(Vu0 &v) {
    convert_weights(v, 3);
    skin4_tail(v);
}
void prog_0308(Vu0 &v) { skin4_tail(v); }

// 0x478 (0x460 converts the weights first): a vertex position skinned by four bones without a normal.
void skin4_position(Vu0 &v) {
    v.mul(2, 2, 4, 'x');
    v.mul(5, 2, 1, 'x');
    v.mul(6, 2, 1, 'y');
    v.mul(7, 2, 1, 'z');
    v.mul(2, 2, 1, 'w');
    v.mul(ACC, 8, 5, 'x');
    v.madd(ACC, 9, 5, 'y');
    v.madd(ACC, 10, 5, 'z');
    v.madd(ACC, 11, 1, 'x');
    v.madd(ACC, 12, 6, 'x');
    v.madd(ACC, 13, 6, 'y');
    v.madd(ACC, 14, 6, 'z');
    v.madd(ACC, 15, 1, 'y');
    v.madd(ACC, 16, 7, 'x');
    v.madd(ACC, 17, 7, 'y');
    v.madd(ACC, 18, 7, 'z');
    v.madd(ACC, 19, 1, 'z');
    v.madd(ACC, 20, 2, 'x');
    v.madd(ACC, 21, 2, 'y');
    v.madd(ACC, 22, 2, 'z');
    v.madd(2, 23, 1, 'w');
}
void prog_0460(Vu0 &v) {
    convert_weights(v, 0);
    skin4_position(v);
}
void prog_0478(Vu0 &v) { skin4_position(v); }

// 0x548: vf4 = atan((vf2 - vf1) / (vf2 + vf1)) + pi/4 (lane x drives the division), an odd polynomial in
// t = that ratio; vf8 = (1, 1, 1, 2)
void prog_0548(Vu0 &v) {
    v.add(5, 2, 1);
    v.sub(6, 2, 1);
    v.div(6, 'x', 5, 'x');
    v.s.i = fbits(0x3F800000u);
    v.addi(8, 0);
    v.addq(1, 0);
    v.mul(7, 1, 1);
    v.s.i = fbits(0x3F7FFFF5u);
    v.muli(ACC, 1);
    v.mul(5, 7, 1);
    v.mul(6, 5, 7);
    v.s.i = fbits(0xBEAAA61Cu);
    v.maddi(ACC, 5);
    v.mul(5, 6, 7);
    v.s.i = fbits(0x3E4C40A6u);
    v.maddi(ACC, 6);
    v.mul(6, 5, 7);
    v.s.i = fbits(0xBE0E6C63u);
    v.maddi(ACC, 5);
    v.mul(5, 6, 7);
    v.s.i = fbits(0x3DC577DFu);
    v.maddi(ACC, 6);
    v.mul(6, 5, 7);
    v.s.i = fbits(0xBD6501C4u);
    v.maddi(ACC, 5);
    v.mul(5, 6, 7);
    v.s.i = fbits(0x3CB31652u);
    v.maddi(ACC, 6);
    v.s.i = fbits(0x3F490FDBu);  // pi/4
    v.maddi(ACC, 8);
    v.s.i = fbits(0xBB84D7E7u);
    v.maddi(4, 5);
}

// 0x678: one point to the screen: vf1 = vf16..vf19 * (vf1, 1), clamped by vf22, divided by w, offset by
// vf20 (ACC = vf21 w), to 12.4 fixed point; vf3 (a colour) clamped to 0..1, scaled by vf15, to integers;
// vf2 scaled by 1/w with z = 1 (texture coordinates)
void prog_0678(Vu0 &v) {
    v.mul(ACC, 16, 1, 'x');
    v.madd(ACC, 17, 1, 'y');
    v.madd(ACC, 18, 1, 'z');
    v.madd(1, 19, 0, 'w');
    v.max(3, 3, 0, 'x');
    v.add(2, 0, 0, 'w', Z);
    v.div(0, 'w', 1, 'w');
    v.mini(3, 3, 0, 'w');
    v.mini(1, 1, 22);
    v.mul(3, 3, 15);
    v.mul(ACC, 21, 0, 'w');
    v.mulq(1, 1);
    v.mulq(2, 2);
    v.madd(1, 1, 20);
    v.ftoi(3, 3, 1.0f, XYZW);
    v.ftoi(1, 1, 16.0f, XYZW);
}

// 0x700: four points (vf1..vf4) to the screen as 0x678 does, x and y in 12.4, z and w as integers
void prog_0700(Vu0 &v) {
    v.mul(ACC, 16, 1, 'x');
    v.madd(ACC, 17, 1, 'y');
    v.madd(ACC, 18, 1, 'z');
    v.madd(1, 19, 0, 'w');
    v.mul(ACC, 16, 2, 'x');
    v.madd(ACC, 17, 2, 'y');
    v.div(0, 'w', 1, 'w');
    v.madd(ACC, 18, 2, 'z');
    v.madd(2, 19, 0, 'w');
    v.mini(1, 1, 22);
    v.mul(ACC, 16, 3, 'x');
    v.madd(ACC, 17, 3, 'y');
    v.mulq(1, 1);
    v.div(0, 'w', 2, 'w');
    v.madd(ACC, 18, 3, 'z');
    v.madd(3, 19, 0, 'w');
    v.mini(2, 2, 22);
    v.mul(ACC, 16, 4, 'x');
    v.madd(ACC, 17, 4, 'y');
    v.mulq(2, 2);
    v.div(0, 'w', 3, 'w');
    v.madd(ACC, 18, 4, 'z');
    v.madd(4, 19, 0, 'w');
    v.mini(3, 3, 22);
    v.mul(ACC, 21, 0, 'w');
    v.mulq(3, 3);
    v.div(0, 'w', 4, 'w');
    v.madd(1, 1, 20);
    v.mini(4, 4, 22);
    v.mul(ACC, 21, 0, 'w');
    v.madd(2, 2, 20);
    v.mulq(4, 4);
    v.mul(ACC, 21, 0, 'w');
    v.madd(3, 3, 20);
    v.mul(ACC, 21, 0, 'w');
    v.madd(4, 4, 20);
    for (int k = 1; k <= 4; ++k) v.ftoi(k, k, 16.0f, X | Y);
    for (int k = 1; k <= 4; ++k) v.ftoi(k, k, 1.0f, Z | W);
}

// 0x858: vf24..vf27 = (vf1..vf4) * (vf24..vf27), then rows 0..2 scaled by vf5's x, y, z
void prog_0858(Vu0 &v) {
    for (int r = 24; r <= 27; ++r) {
        v.mul(ACC, 1, r, 'x');
        v.madd(ACC, 2, r, 'y');
        v.madd(ACC, 3, r, 'z');
        v.madd(r, 4, r, 'w');
    }
    v.mul(24, 24, 5, 'x');
    v.mul(25, 25, 5, 'y');
    v.mul(26, 26, 5, 'z');
}

// 0x900 / 0x930: push / pop the current matrix (vf28..vf31) on a stack in VU0 data memory at vi13
void prog_0900(Vu0 &v) {
    for (int r = 28; r <= 31; ++r) {
        std::memcpy(v.mem(v.s.vi[13]), v.s.vf[r], 16);
        v.set_vi(13, v.s.vi[13] + 1);
    }
}
void prog_0930(Vu0 &v) {
    for (int r = 31; r >= 28; --r) {
        v.set_vi(13, v.s.vi[13] - 1);
        std::memcpy(v.s.vf[r], v.mem(v.s.vi[13]), 16);
    }
}

}  // namespace

bool vu0_program(unsigned int addr, Gt4Vu0State &s, void *data) {
    Vu0 v{s, static_cast<unsigned char *>(data)};
    switch (addr) {
    case 0x58: prog_0058(v); return true;
    case 0x78: prog_0078(v); return true;
    case 0xB8: prog_00B8(v); return true;
    case 0xF8: prog_00F8(v); return true;
    case 0x2E8: prog_02E8(v); return true;
    case 0x308: prog_0308(v); return true;
    case 0x460: prog_0460(v); return true;
    case 0x478: prog_0478(v); return true;
    case 0x548: prog_0548(v); return true;
    case 0x678: prog_0678(v); return true;
    case 0x700: prog_0700(v); return true;
    case 0x858: prog_0858(v); return true;
    case 0x900: prog_0900(v); return true;
    case 0x930: prog_0930(v); return true;
    default: return false;
    }
}

}  // namespace vu0ref
