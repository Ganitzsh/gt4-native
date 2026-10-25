// VU0 on the native game side: the register file of the whole program and a micro-mode
// interpreter for vcallms.
//
// The game's VU0 code reaches this file two ways: macro-mode instructions in inline asm, which
// tools/game_cc.py translates into C over __gt4_vu0 (compat/game/gt4_asm.h), and native
// replacements (runtime/native/*.cpp) of the hand-written VU0 helpers. VU0 microprograms
// (vcallms ADDR) run here, from VU0 micro memory at 0x11000000 (plain memory in this process).
//
// The game uploads its VU0 microcode once, in 004A03C0 (graphics library start-up): 0xA80 bytes
// from 0x006D3280 to VU0 address 0, through VIF0 MPG (004A12B8). The runtime streams VIF0 to the
// host instead of unpacking it, so the first vcallms copies that block into micro memory if it is
// still empty.
//
// Execution model (enough for the game's microcode, checked against it, see docs):
// - one instruction pair per step; the upper (FMAC) instruction reads its operands before the
//   lower one runs and writes its result after it, so a lower instruction of the same pair sees the
//   old registers, and an upper instruction sees I and Q as they were before the pair (LOI and DIV
//   of the same pair take effect after it, as the hardware's pipeline does);
// - FMAC results are otherwise visible to the next pair (the hardware interlocks on them);
// - DIV/SQRT/RSQRT put their result in Q when WAITQ runs, or after their latency (7/7/13 pairs);
// - branches have one delay slot; the E bit ends the program after the next pair;
// - VU data memory is 4 KB at 0x11004000 (addresses wrap at 256 quadwords).
// Unknown instructions stop the game with their address and encoding.
#include "vu0.h"

#include "guest.h"
#include "log.h"

#include <cmath>
#include <cstring>

using namespace gt4::rt;

extern "C" {
Gt4Vu0State __gt4_vu0 = {
    {{0.0f, 0.0f, 0.0f, 1.0f}},  // vf0 = (0, 0, 0, 1)
    {0, 0, 0, 0}, 0.0f, 0.0f, 0.0f, 0.0f, 0x3F800000u, 0, {0},
};
}

namespace {

constexpr uint32_t kMicroMem = 0x11000000;
constexpr uint32_t kMicroSize = 0x1000;
constexpr uint32_t kDataMem = 0x11004000;
constexpr uint32_t kGameMicrocode = 0x006D3280;  // 004A03C0: func_004A12B8(0, 0, 0xA80, 0x006D3280)
constexpr uint32_t kGameMicrocodeSize = 0xA80;

bool g_loaded = false;

void load_microcode() {
    auto *micro = static_cast<uint8_t *>(guest_ptr(kMicroMem));
    bool empty = true;
    for (uint32_t k = 0; k < kMicroSize && empty; ++k) empty = micro[k] == 0;
    if (empty) std::memcpy(micro, guest_ptr(kGameMicrocode), kGameMicrocodeSize);
    g_loaded = true;
}

float *mem_qw(uint32_t qw) { return static_cast<float *>(guest_ptr(kDataMem + ((qw & 0xFF) << 4))); }

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

// The VU has no infinities: a division by zero gives the largest float of the right sign.
float vu_div(float a, float b) {
    if (b == 0.0f) return fbits(((ubits(a) ^ ubits(b)) & 0x80000000u) | 0x7F7FFFFFu);
    return a / b;
}

int32_t ftoi(float x) {
    if (x >= 2147483648.0f) return 0x7FFFFFFF;
    if (x <= -2147483648.0f) return static_cast<int32_t>(0x80000000u);
    if (x != x) return 0;
    return static_cast<int32_t>(x);  // toward zero
}

[[noreturn]] void unknown(const char *what, uint32_t pc, uint32_t word) {
    fatal("VU0 microprogram: unknown %s instruction %08x at %04x", what, word, pc);
}

struct Pending {
    bool active = false;
    float value = 0;
    int left = 0;
};

struct Upper {
    bool write = false;
    bool to_acc = false;
    int reg = 0;
    int mask = 0;
    float v[4] = {};
};

// The upper instruction: its result, computed from the registers before the pair.
Upper upper(Gt4Vu0State &s, uint32_t u, uint32_t pc) {
    Upper r;
    const int dest = (u >> 21) & 15;  // bit 24 x ... bit 21 w
    r.mask = ((dest >> 3) & 1) | ((dest >> 1) & 2) | ((dest << 1) & 4) | ((dest << 3) & 8);
    const int ft = (u >> 16) & 31, fs = (u >> 11) & 31, fd = (u >> 6) & 31;
    const int bc = u & 3;
    const float *S = s.vf[fs], *T = s.vf[ft];
    const int op = u & 0x3F;
    int kind;            // 0 add, 1 sub, 2 madd, 3 msub, 4 max, 5 mini, 6 mul
    int src;             // 0 vector, 1 bc, 2 q, 3 i
    bool acc = false;
    if (op < 0x3C) {
        r.reg = fd;
        if (op < 0x1C) {
            kind = op >> 2;
            src = 1;
        } else {
            switch (op) {
            case 0x1C: kind = 6; src = 2; break;
            case 0x1D: kind = 4; src = 3; break;
            case 0x1E: kind = 6; src = 3; break;
            case 0x1F: kind = 5; src = 3; break;
            case 0x20: kind = 0; src = 2; break;
            case 0x21: kind = 2; src = 2; break;
            case 0x22: kind = 0; src = 3; break;
            case 0x23: kind = 2; src = 3; break;
            case 0x24: kind = 1; src = 2; break;
            case 0x25: kind = 3; src = 2; break;
            case 0x26: kind = 1; src = 3; break;
            case 0x27: kind = 3; src = 3; break;
            case 0x28: kind = 0; src = 0; break;
            case 0x29: kind = 2; src = 0; break;
            case 0x2A: kind = 6; src = 0; break;
            case 0x2B: kind = 4; src = 0; break;
            case 0x2C: kind = 1; src = 0; break;
            case 0x2D: kind = 3; src = 0; break;
            case 0x2E:  // OPMSUB fd.xyz = ACC.xyz - fs.yzx * ft.zxy
                r.write = true;
                r.v[0] = s.acc[0] - S[1] * T[2];
                r.v[1] = s.acc[1] - S[2] * T[0];
                r.v[2] = s.acc[2] - S[0] * T[1];
                r.mask &= 7;
                return r;
            case 0x2F: kind = 5; src = 0; break;
            default: unknown("upper", pc, u);
            }
        }
    } else {
        const int sp = (((u >> 6) & 31) << 2) | bc;
        acc = true;
        r.reg = fs;
        if (sp < 0x10) {
            kind = sp >> 2;
            src = 1;
        } else if (sp < 0x18 || sp == 0x1D) {  // ITOF / FTOI / ABS: ft = f(fs)
            static const float scale[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};
            r.write = true;
            r.reg = ft;
            for (int k = 0; k < 4; ++k) {
                if (sp == 0x1D) r.v[k] = std::fabs(S[k]);
                else if (sp < 0x14) r.v[k] = static_cast<float>(static_cast<int32_t>(ubits(S[k]))) / scale[sp & 3];
                else r.v[k] = fbits(static_cast<uint32_t>(ftoi(S[k] * scale[sp & 3])));
            }
            return r;
        } else if (sp < 0x1C) {
            kind = 6;
            src = 1;
        } else {
            switch (sp) {
            case 0x1C: kind = 6; src = 2; break;
            case 0x1E: kind = 6; src = 3; break;
            case 0x1F: {  // CLIPw.xyz fs, ft.w
                const float w = std::fabs(T[3]);
                uint32_t f = 0;
                if (S[0] > w) f |= 1;
                if (S[0] < -w) f |= 2;
                if (S[1] > w) f |= 4;
                if (S[1] < -w) f |= 8;
                if (S[2] > w) f |= 16;
                if (S[2] < -w) f |= 32;
                s.clip = ((s.clip << 6) | f) & 0xFFFFFF;
                return r;
            }
            case 0x20: kind = 0; src = 2; break;
            case 0x21: kind = 2; src = 2; break;
            case 0x22: kind = 0; src = 3; break;
            case 0x23: kind = 2; src = 3; break;
            case 0x24: kind = 1; src = 2; break;
            case 0x25: kind = 3; src = 2; break;
            case 0x26: kind = 1; src = 3; break;
            case 0x27: kind = 3; src = 3; break;
            case 0x28: kind = 0; src = 0; break;
            case 0x29: kind = 2; src = 0; break;
            case 0x2A: kind = 6; src = 0; break;
            case 0x2C: kind = 1; src = 0; break;
            case 0x2D: kind = 3; src = 0; break;
            case 0x2E:  // OPMULA ACC.xyz = fs.yzx * ft.zxy
                r.write = true;
                r.to_acc = true;
                r.v[0] = S[1] * T[2];
                r.v[1] = S[2] * T[0];
                r.v[2] = S[0] * T[1];
                r.mask &= 7;
                return r;
            case 0x2F: return r;  // NOP
            default: unknown("upper", pc, u);
            }
        }
    }
    r.write = true;
    r.to_acc = acc;
    for (int k = 0; k < 4; ++k) {
        if (!(r.mask & (1 << k))) continue;
        const float a = S[k];
        const float b = src == 0 ? T[k] : src == 1 ? T[bc] : src == 2 ? s.q : s.i;
        float v;
        switch (kind) {
        case 0: v = a + b; break;
        case 1: v = a - b; break;
        case 2: v = s.acc[k] + a * b; break;
        case 3: v = s.acc[k] - a * b; break;
        case 4: v = a > b ? a : b; break;
        case 5: v = a < b ? a : b; break;
        default: v = a * b; break;
        }
        r.v[k] = v;
    }
    return r;
}

int mask_of(uint32_t l) {
    const int dest = (l >> 21) & 15;
    return ((dest >> 3) & 1) | ((dest >> 1) & 2) | ((dest << 1) & 4) | ((dest << 3) & 8);
}

void set_vi(Gt4Vu0State &s, int n, uint32_t v) {
    if (n != 0) s.vi[n] = v & 0xFFFF;
}

}  // namespace

extern "C" void __gt4_vu0_callms(unsigned int addr) {
    if (!g_loaded) load_microcode();
    Gt4Vu0State &s = __gt4_vu0;
    const auto *code = static_cast<const uint32_t *>(guest_ptr(kMicroMem));
    uint32_t pc = addr & (kMicroSize - 8);
    int64_t branch_to = -1;  // taken branch: next pc after the delay slot
    bool end_next = false;
    Pending q;
    for (long steps = 0;; ++steps) {
        if (steps > 1000000) fatal("VU0 microprogram at %04x: no end after a million instructions", addr);
        const uint32_t lo = code[pc / 4], up = code[pc / 4 + 1];
        if (q.active && --q.left <= 0) {
            s.q = q.value;
            q.active = false;
        }
        const Upper u = upper(s, up, pc);
        int64_t new_branch = -1;
        if (up & 0x80000000u) {
            s.i = fbits(lo);  // LOI (after the upper instruction read I)
        } else {
            const uint32_t op = lo >> 25;
            const int it = (lo >> 16) & 31, is = (lo >> 11) & 31, id = (lo >> 6) & 31;
            const int mask = mask_of(lo);
            int32_t imm11 = lo & 0x7FF;
            if (imm11 & 0x400) imm11 -= 0x800;
            if (op == 0x40) {
                const uint32_t f = lo & 0x3F;
                if (f < 0x3C) {
                    switch (f) {
                    case 0x30: set_vi(s, id, s.vi[is] + s.vi[it]); break;  // IADD
                    case 0x31: set_vi(s, id, s.vi[is] - s.vi[it]); break;  // ISUB
                    case 0x32: {                                           // IADDI
                        int32_t imm5 = (lo >> 6) & 31;
                        if (imm5 & 16) imm5 -= 32;
                        set_vi(s, it, s.vi[is] + imm5);
                        break;
                    }
                    case 0x34: set_vi(s, id, s.vi[is] & s.vi[it]); break;  // IAND
                    case 0x35: set_vi(s, id, s.vi[is] | s.vi[it]); break;  // IOR
                    default: unknown("lower", pc, lo);
                    }
                } else {
                    const uint32_t sp = (((lo >> 6) & 31) << 2) | (lo & 3);
                    const int fsf = (lo >> 21) & 3, ftf = (lo >> 23) & 3;
                    switch (sp) {
                    case 0x30:  // MOVE ft, fs
                        if (it) for (int k = 0; k < 4; ++k) if (mask & (1 << k)) s.vf[it][k] = s.vf[is][k];
                        break;
                    case 0x31: {  // MR32 ft, fs
                        float t[4] = {s.vf[is][1], s.vf[is][2], s.vf[is][3], s.vf[is][0]};
                        if (it) for (int k = 0; k < 4; ++k) if (mask & (1 << k)) s.vf[it][k] = t[k];
                        break;
                    }
                    case 0x34:    // LQI ft, (is++)
                    case 0x36: {  // LQD ft, (--is)
                        if (sp == 0x36) set_vi(s, is, s.vi[is] - 1);
                        const float *m = mem_qw(s.vi[is]);
                        if (it) for (int k = 0; k < 4; ++k) if (mask & (1 << k)) s.vf[it][k] = m[k];
                        if (sp == 0x34) set_vi(s, is, s.vi[is] + 1);
                        break;
                    }
                    case 0x35:    // SQI fs, (it++)
                    case 0x37: {  // SQD fs, (--it)
                        if (sp == 0x37) set_vi(s, it, s.vi[it] - 1);
                        float *m = mem_qw(s.vi[it]);
                        for (int k = 0; k < 4; ++k) if (mask & (1 << k)) m[k] = s.vf[is][k];
                        if (sp == 0x35) set_vi(s, it, s.vi[it] + 1);
                        break;
                    }
                    case 0x38:  // DIV Q, fs.fsf, ft.ftf
                        q = {true, vu_div(s.vf[is][fsf], s.vf[it][ftf]), 7};
                        break;
                    case 0x39:  // SQRT Q, ft.ftf
                        q = {true, std::sqrt(std::fabs(s.vf[it][ftf])), 7};
                        break;
                    case 0x3A:  // RSQRT Q, fs.fsf, ft.ftf
                        q = {true, vu_div(s.vf[is][fsf], std::sqrt(std::fabs(s.vf[it][ftf]))), 13};
                        break;
                    case 0x3B:  // WAITQ
                        if (q.active) {
                            s.q = q.value;
                            q.active = false;
                        }
                        break;
                    case 0x3C:  // MTIR it, fs.fsf
                        set_vi(s, it, ubits(s.vf[is][fsf]));
                        break;
                    case 0x3D:  // MFIR ft, is
                        if (it) for (int k = 0; k < 4; ++k)
                                if (mask & (1 << k)) s.vf[it][k] = fbits(static_cast<uint32_t>(static_cast<int16_t>(s.vi[is])));
                        break;
                    case 0x3E:  // ILWR it, (is)
                        set_vi(s, it, ubits(mem_qw(s.vi[is])[__builtin_ctz(mask | 16)]));
                        break;
                    case 0x3F:  // ISWR it, (is)
                        for (int k = 0; k < 4; ++k) if (mask & (1 << k)) mem_qw(s.vi[is])[k] = fbits(s.vi[it]);
                        break;
                    case 0x40:  // RNEXT ft, R
                    case 0x41: {  // RGET ft, R
                        if (sp == 0x40) {
                            const uint32_t x = s.r;
                            s.r = 0x3F800000u | (((x << 1) | (((x >> 4) ^ (x >> 22)) & 1)) & 0x7FFFFFu);
                        }
                        if (it) for (int k = 0; k < 4; ++k) if (mask & (1 << k)) s.vf[it][k] = fbits(s.r);
                        break;
                    }
                    case 0x42: s.r = 0x3F800000u | (ubits(s.vf[is][fsf]) & 0x7FFFFFu); break;  // RINIT
                    case 0x43: s.r = 0x3F800000u | ((s.r ^ ubits(s.vf[is][fsf])) & 0x7FFFFFu); break;  // RXOR
                    default: unknown("lower", pc, lo);
                    }
                }
            } else {
                switch (op) {
                case 0x00: {  // LQ ft, imm(is)
                    const float *m = mem_qw(s.vi[is] + imm11);
                    if (it) for (int k = 0; k < 4; ++k) if (mask & (1 << k)) s.vf[it][k] = m[k];
                    break;
                }
                case 0x01: {  // SQ fs, imm(it)
                    float *m = mem_qw(s.vi[it] + imm11);
                    for (int k = 0; k < 4; ++k) if (mask & (1 << k)) m[k] = s.vf[is][k];
                    break;
                }
                case 0x04:  // ILW it, imm(is)
                    set_vi(s, it, ubits(mem_qw(s.vi[is] + imm11)[__builtin_ctz(mask | 16)]));
                    break;
                case 0x05:  // ISW it, imm(is)
                    for (int k = 0; k < 4; ++k) if (mask & (1 << k)) mem_qw(s.vi[is] + imm11)[k] = fbits(s.vi[it]);
                    break;
                case 0x08:    // IADDIU it, is, imm15
                case 0x09: {  // ISUBIU
                    const uint32_t imm15 = (lo & 0x7FF) | ((lo >> 10) & 0x7800);
                    set_vi(s, it, op == 0x08 ? s.vi[is] + imm15 : s.vi[is] - imm15);
                    break;
                }
                case 0x20:  // B
                    new_branch = pc + 8 + imm11 * 8;
                    break;
                case 0x21:  // BAL it, imm
                    set_vi(s, it, (pc + 16) / 8);
                    new_branch = pc + 8 + imm11 * 8;
                    break;
                case 0x24:  // JR is
                    new_branch = s.vi[is] * 8;
                    break;
                case 0x25:  // JALR it, is
                    new_branch = s.vi[is] * 8;
                    set_vi(s, it, (pc + 16) / 8);
                    break;
                case 0x28: case 0x29: case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
                    const int16_t a = static_cast<int16_t>(s.vi[is]), b = static_cast<int16_t>(s.vi[it]);
                    bool take = false;
                    switch (op) {
                    case 0x28: take = a == b; break;   // IBEQ
                    case 0x29: take = a != b; break;   // IBNE
                    case 0x2C: take = a < 0; break;    // IBLTZ
                    case 0x2D: take = a > 0; break;    // IBGTZ
                    case 0x2E: take = a <= 0; break;   // IBLEZ
                    default: take = a >= 0; break;     // IBGEZ
                    }
                    if (take) new_branch = pc + 8 + imm11 * 8;
                    break;
                }
                default: unknown("lower", pc, lo);
                }
            }
        }
        if (u.write) {
            float *d = u.to_acc ? s.acc : s.vf[u.reg];
            if (u.to_acc || u.reg != 0)
                for (int k = 0; k < 4; ++k) if (u.mask & (1 << k)) d[k] = u.v[k];
        }
        // the end bit and branches take effect after the next pair (their delay slot)
        if (end_next) break;
        if (up & 0x40000000u) end_next = true;
        if (branch_to >= 0) {
            pc = static_cast<uint32_t>(branch_to) & (kMicroSize - 8);
            branch_to = -1;
        } else {
            pc = (pc + 8) & (kMicroSize - 8);
        }
        branch_to = new_branch;
    }
    if (q.active) s.q = q.value;  // a division still in flight completes before the next use
}
