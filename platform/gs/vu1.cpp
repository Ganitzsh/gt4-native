// VU1 micro-mode interpreter (development scaffold; see vu1.h for the model and provenance).
#include "vu1.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace gt4::ps2 {
namespace {

inline uint32_t bitsOf(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
inline float floatOf(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

// A VU operand: denormals read as a signed zero, the exponent-255 encodings as +-max (the VU has no
// infinities or NaNs).
inline float vuIn(uint32_t u) {
    const uint32_t e = u & 0x7F800000u;
    if (e == 0) u &= 0x80000000u;
    else if (e == 0x7F800000u) u = (u & 0x80000000u) | 0x7F7FFFFFu;
    return floatOf(u);
}

// A VU result from an exact (or double-precision) value: rounded toward zero to single precision, with
// overflow saturating to +-max (no infinities) and underflow giving a signed zero. `flags` gets
// 1 zero, 2 underflow, 4 overflow.
inline uint32_t vuRound(double r, uint32_t& flags) {
    const uint32_t sign = (std::signbit(r) ? 0x80000000u : 0u);
    const double a = std::fabs(r);
    flags = 0;
    if (a == 0.0 || std::isnan(a)) { flags = 1; return sign; }
    if (a >= 0x1p128) { flags = 4; return sign | 0x7F7FFFFFu; }
    const float f = static_cast<float>(a);
    uint32_t u = bitsOf(f);
    if (static_cast<double>(f) > a) --u;   // rounded up: the next smaller magnitude (toward zero)
    if (u < 0x00800000u) { flags = 3; return sign; }   // below the smallest normal
    return sign | u;
}

// A result without flags (EFU, DIV).
inline uint32_t vuOut(double r) {
    uint32_t flags;
    return vuRound(r, flags);
}

// One field of an FMAC result and its MAC flags (Z, S, U, O groups; x is bit 3 of each group).
inline uint32_t macResult(uint32_t& mac, int shift, double r) {
    uint32_t flags = 0;
    const uint32_t out = vuRound(r, flags);
    uint32_t m = mac & ~(0x1111u << shift);
    if (out & 0x80000000u) m |= 0x10u << shift;
    if (flags & 1) m |= 0x1u << shift;
    if (flags & 2) m |= 0x100u << shift;
    if (flags & 4) m |= 0x1000u << shift;
    mac = m;
    return out;
}

inline uint32_t statusFromMac(uint32_t mac) {
    return ((mac & 0x000F) ? 1u : 0u) | ((mac & 0x00F0) ? 2u : 0u) | ((mac & 0x0F00) ? 4u : 0u) | ((mac & 0xF000) ? 8u : 0u);
}

// MAX / MINI compare the encodings as sign-magnitude integers (well defined for every bit pattern).
inline uint32_t fpMax(uint32_t a, uint32_t b) {
    const int32_t x = static_cast<int32_t>(a), y = static_cast<int32_t>(b);
    return static_cast<uint32_t>((x < 0 && y < 0) ? std::min(x, y) : std::max(x, y));
}
inline uint32_t fpMin(uint32_t a, uint32_t b) {
    const int32_t x = static_cast<int32_t>(a), y = static_cast<int32_t>(b);
    return static_cast<uint32_t>((x < 0 && y < 0) ? std::max(x, y) : std::min(x, y));
}

uint32_t floatToInt(uint32_t u, uint32_t shift) {
    float f = floatOf(u);
    if (shift) f *= floatOf(0x3F800000u + (shift << 23));
    const uint32_t v = bitsOf(f);
    if ((v & 0x7F800000u) >= 0x4F000000u) return (v & 0x80000000u) ? 0x80000000u : 0x7FFFFFFFu;
    return static_cast<uint32_t>(static_cast<int32_t>(f));
}

uint32_t intToFloat(uint32_t u, uint32_t shift) {
    return vuOut(std::ldexp(static_cast<double>(static_cast<int32_t>(u)), -static_cast<int>(shift)));
}

constexpr uint32_t kAcc = 32;
constexpr uint32_t fieldMask(uint32_t c) { return 8u >> c; }   // dest bit of field c (x = 0)

// EFU latencies (cycles until P is written).
int efuLatency(uint32_t ext) {
    switch (ext) {
    case 0x70: return 11;  // ESADD
    case 0x71: return 18;  // ERSADD
    case 0x72: return 18;  // ELENG
    case 0x73: return 24;  // ERLENG
    case 0x74: case 0x75: return 54;  // EATANxy, EATANxz
    case 0x76: return 12;  // ESUM
    case 0x78: return 12;  // ESQRT
    case 0x79: return 18;  // ERSQRT
    case 0x7A: return 12;  // ERCPR
    case 0x7C: return 29;  // ESIN
    case 0x7D: return 54;  // EATAN
    case 0x7E: return 44;  // EEXP
    default: return 1;
    }
}

}  // namespace

// The result of the upper instruction, committed after the lower one has read the old registers.
struct UpperResult {
    int reg = -1;          // 0..31 VF, 32 ACC, -1 nothing
    uint32_t mask = 0;
    VuVector value{};
    bool flags = false;    // updated MAC/status
    bool clipOp = false;   // CLIP
};

struct VuExec {
    Vu1& vu;
    uint32_t code = 0;
    uint32_t dest() const { return (code >> 21) & 0xF; }
    uint32_t ft() const { return (code >> 16) & 0x1F; }
    uint32_t fs() const { return (code >> 11) & 0x1F; }
    uint32_t fd() const { return (code >> 6) & 0x1F; }
    uint32_t it() const { return ft() & 0xF; }
    uint32_t is() const { return fs() & 0xF; }
    uint32_t id() const { return fd() & 0xF; }
    uint32_t fsf() const { return (code >> 21) & 3; }
    uint32_t ftf() const { return (code >> 23) & 3; }
    int32_t imm11() const { return (code & 0x400) ? static_cast<int32_t>(code & 0x3FF) - 0x400 : static_cast<int32_t>(code & 0x3FF); }

    // --- upper ---
    enum Arith { Add, Sub, Mul, Madd, Msub };

    void arith(UpperResult& out, Arith a, uint32_t dst, const uint32_t second[4]) {
        const uint32_t d = dest();
        const VuVector& s = vu.vf[fs()];
        for (uint32_t c = 0; c < 4; ++c) {
            const int shift = static_cast<int>(3 - c);
            if (!(d & fieldMask(c))) { vu.macInternal &= ~(0x1111u << shift); continue; }
            const double x = vuIn(s.u[c]), y = vuIn(second[c]);
            double r = 0.0;
            switch (a) {
            case Add: r = x + y; break;
            case Sub: r = x - y; break;
            case Mul: r = x * y; break;   // exact in double
            case Madd: r = static_cast<double>(vuIn(vu.acc.u[c])) + static_cast<double>(floatOf(vuOut(x * y))); break;
            case Msub: r = static_cast<double>(vuIn(vu.acc.u[c])) - static_cast<double>(floatOf(vuOut(x * y))); break;
            }
            out.value.u[c] = macResult(vu.macInternal, shift, r);
        }
        vu.statusInternal = (vu.statusInternal & ~0xFu) | statusFromMac(vu.macInternal);
        out.reg = (dst == 0) ? -1 : static_cast<int>(dst);
        out.mask = d;
        out.flags = true;
    }

    void minmax(UpperResult& out, bool max, const uint32_t second[4]) {
        const VuVector& s = vu.vf[fs()];
        for (uint32_t c = 0; c < 4; ++c) out.value.u[c] = max ? fpMax(s.u[c], second[c]) : fpMin(s.u[c], second[c]);
        out.reg = fd() ? static_cast<int>(fd()) : -1;
        out.mask = dest();
    }

    void unary(UpperResult& out, uint32_t (*fn)(uint32_t, uint32_t), uint32_t shift) {
        const VuVector& s = vu.vf[fs()];
        for (uint32_t c = 0; c < 4; ++c) out.value.u[c] = fn(s.u[c], shift);
        out.reg = ft() ? static_cast<int>(ft()) : -1;
        out.mask = dest();
    }

    // Reads (for stalls) and executes the upper instruction.
    void upperStalls(uint32_t upper) {
        code = upper;
        const uint32_t op = upper & 0x3F;
        const uint32_t d = dest();
        auto vector = [&] { vu.stallOn(fs(), d); vu.stallOn(ft(), d); };
        auto broadcast = [&](uint32_t bc) { vu.stallOn(fs(), d); vu.stallOn(ft(), fieldMask(bc)); };
        if (op < 0x1C) {
            broadcast(op & 3);
            if ((op >> 2) == 2 || (op >> 2) == 3) vu.stallOn(kAcc, d);
        } else if (op < 0x28) {
            vu.stallOn(fs(), d);
            if (op == 0x21 || op == 0x23 || op == 0x25 || op == 0x27) vu.stallOn(kAcc, d);
        } else if (op < 0x30) {
            vector();
            if (op == 0x29 || op == 0x2D || op == 0x2E) vu.stallOn(kAcc, d);
        } else if (op >= 0x3C) {
            const uint32_t ext = (((upper >> 6) & 0x1F) << 2) | (upper & 3);
            if (ext < 0x10 || (ext >= 0x18 && ext < 0x1C)) {
                broadcast(ext & 3);
                if ((ext >> 2) == 2 || (ext >> 2) == 3) vu.stallOn(kAcc, d);
            } else if (ext < 0x18 || ext == 0x1D) {
                vu.stallOn(fs(), d);
            } else if (ext == 0x1F) {
                vu.stallOn(fs(), 0xE);
                vu.stallOn(ft(), 0x1);
            } else if (ext >= 0x20 && ext < 0x28) {
                vu.stallOn(fs(), d);
                if (ext == 0x21 || ext == 0x23 || ext == 0x25 || ext == 0x27) vu.stallOn(kAcc, d);
            } else if (ext >= 0x28 && ext < 0x2F) {
                vector();
                if (ext == 0x29 || ext == 0x2D) vu.stallOn(kAcc, d);
            }
        }
    }

    UpperResult upper(uint32_t upper) {
        code = upper;
        UpperResult out;
        const uint32_t op = upper & 0x3F;
        uint32_t second[4];
        auto bc = [&](uint32_t v) { second[0] = second[1] = second[2] = second[3] = v; return second; };
        const VuVector& t = vu.vf[ft()];
        if (op < 0x1C) {
            const uint32_t* s = bc(t.u[op & 3]);
            switch (op >> 2) {
            case 0: arith(out, Add, fd(), s); break;
            case 1: arith(out, Sub, fd(), s); break;
            case 2: arith(out, Madd, fd(), s); break;
            case 3: arith(out, Msub, fd(), s); break;
            case 4: minmax(out, true, s); break;
            case 5: minmax(out, false, s); break;
            case 6: arith(out, Mul, fd(), s); break;
            }
            return out;
        }
        switch (op) {
        case 0x1C: arith(out, Mul, fd(), bc(vu.q)); return out;
        case 0x1D: minmax(out, true, bc(vu.i)); return out;
        case 0x1E: arith(out, Mul, fd(), bc(vu.i)); return out;
        case 0x1F: minmax(out, false, bc(vu.i)); return out;
        case 0x20: arith(out, Add, fd(), bc(vu.q)); return out;
        case 0x21: arith(out, Madd, fd(), bc(vu.q)); return out;
        case 0x22: arith(out, Add, fd(), bc(vu.i)); return out;
        case 0x23: arith(out, Madd, fd(), bc(vu.i)); return out;
        case 0x24: arith(out, Sub, fd(), bc(vu.q)); return out;
        case 0x25: arith(out, Msub, fd(), bc(vu.q)); return out;
        case 0x26: arith(out, Sub, fd(), bc(vu.i)); return out;
        case 0x27: arith(out, Msub, fd(), bc(vu.i)); return out;
        case 0x28: arith(out, Add, fd(), t.u); return out;
        case 0x29: arith(out, Madd, fd(), t.u); return out;
        case 0x2A: arith(out, Mul, fd(), t.u); return out;
        case 0x2B: minmax(out, true, t.u); return out;
        case 0x2C: arith(out, Sub, fd(), t.u); return out;
        case 0x2D: arith(out, Msub, fd(), t.u); return out;
        case 0x2E: opmsub(out); return out;
        case 0x2F: minmax(out, false, t.u); return out;
        default: break;
        }
        if (op < 0x3C) return out;   // undefined: nothing
        const uint32_t ext = (((upper >> 6) & 0x1F) << 2) | (upper & 3);
        if (ext < 0x10) {
            const uint32_t* s = bc(t.u[ext & 3]);
            static const Arith kinds[4] = {Add, Sub, Madd, Msub};
            arith(out, kinds[ext >> 2], kAcc, s);
            return out;
        }
        if (ext < 0x14) { static const uint32_t shifts[4] = {0, 4, 12, 15}; unary(out, intToFloat, shifts[ext & 3]); return out; }
        if (ext < 0x18) { static const uint32_t shifts[4] = {0, 4, 12, 15}; unary(out, floatToInt, shifts[ext & 3]); return out; }
        if (ext < 0x1C) { arith(out, Mul, kAcc, bc(t.u[ext & 3])); return out; }
        switch (ext) {
        case 0x1C: arith(out, Mul, kAcc, bc(vu.q)); break;
        case 0x1D: unary(out, [](uint32_t v, uint32_t) { return v & 0x7FFFFFFFu; }, 0); break;
        case 0x1E: arith(out, Mul, kAcc, bc(vu.i)); break;
        case 0x1F: clipOp(out); break;
        case 0x20: arith(out, Add, kAcc, bc(vu.q)); break;
        case 0x21: arith(out, Madd, kAcc, bc(vu.q)); break;
        case 0x22: arith(out, Add, kAcc, bc(vu.i)); break;
        case 0x23: arith(out, Madd, kAcc, bc(vu.i)); break;
        case 0x24: arith(out, Sub, kAcc, bc(vu.q)); break;
        case 0x25: arith(out, Msub, kAcc, bc(vu.q)); break;
        case 0x26: arith(out, Sub, kAcc, bc(vu.i)); break;
        case 0x27: arith(out, Msub, kAcc, bc(vu.i)); break;
        case 0x28: arith(out, Add, kAcc, t.u); break;
        case 0x29: arith(out, Madd, kAcc, t.u); break;
        case 0x2A: arith(out, Mul, kAcc, t.u); break;
        case 0x2C: arith(out, Sub, kAcc, t.u); break;
        case 0x2D: arith(out, Msub, kAcc, t.u); break;
        case 0x2E: opmula(out); break;
        default: break;   // NOP and undefined
        }
        return out;
    }

    // OPMULA: ACC.xyz = fs.yzx * ft.zxy; OPMSUB: fd.xyz = ACC.xyz - fs.yzx * ft.zxy.
    void opmula(UpperResult& out) { outerProduct(out, false); }
    void opmsub(UpperResult& out) { outerProduct(out, true); }
    void outerProduct(UpperResult& out, bool sub) {
        const VuVector& s = vu.vf[fs()];
        const VuVector& t = vu.vf[ft()];
        const uint32_t a[3] = {1, 2, 0}, b[3] = {2, 0, 1};
        for (uint32_t c = 0; c < 3; ++c) {
            const double product = static_cast<double>(vuIn(s.u[a[c]])) * vuIn(t.u[b[c]]);
            const double r = sub ? static_cast<double>(vuIn(vu.acc.u[c])) - floatOf(vuOut(product)) : product;
            out.value.u[c] = macResult(vu.macInternal, static_cast<int>(3 - c), r);
        }
        vu.macInternal &= ~0x1111u;   // w
        vu.statusInternal = (vu.statusInternal & ~0xFu) | statusFromMac(vu.macInternal);
        out.mask = 0xE;
        out.flags = true;
        out.reg = sub ? (fd() ? static_cast<int>(fd()) : -1) : static_cast<int>(kAcc);
    }

    void clipOp(UpperResult& out) {
        const VuVector& s = vu.vf[fs()];
        int32_t w = static_cast<int32_t>(vu.vf[ft()].u[3]);
        w = (w & 0x7F800000) ? (w & 0x7FFFFFFF) : 0x007FFFFF;
        uint32_t flags = 0;
        for (uint32_t c = 0; c < 3; ++c) {
            if (static_cast<int32_t>(s.u[c]) > w) flags |= 1u << (2 * c);
            if (static_cast<int32_t>(s.u[c] ^ 0x80000000u) > w) flags |= 2u << (2 * c);
        }
        vu.clipInternal = ((vu.clipInternal << 6) | flags) & 0xFFFFFF;
        out.clipOp = true;
    }

    // --- lower ---
    struct LowerInfo {
        int vfWrite = -1;          // VF register written (for the same-register discard rule)
        uint32_t vfMask = 0;
        bool fsset = false, fcset = false;
    };

    static uint32_t ext2(uint32_t lower) { return (((lower >> 6) & 0x1F) << 2) | (lower & 3); }

    LowerInfo lowerInfo(uint32_t lower) {
        code = lower;
        LowerInfo info;
        const uint32_t op = lower >> 25;
        auto vfw = [&](uint32_t reg, uint32_t mask) { if (reg) { info.vfWrite = static_cast<int>(reg); info.vfMask = mask; } };
        if (op == 0x00) vfw(ft(), dest());
        else if (op == 0x11) info.fcset = true;
        else if (op == 0x15) info.fsset = true;
        else if (op == 0x40 && (lower & 0x3C) == 0x3C) {
            switch (ext2(lower)) {
            case 0x30: case 0x31: case 0x34: case 0x36: case 0x3D: case 0x40: case 0x41: case 0x64: vfw(ft(), dest()); break;
            default: break;
            }
        }
        return info;
    }

    // Stalls of the lower instruction (register reads, busy FDIV / EFU units).
    void lowerStalls(uint32_t lower) {
        code = lower;
        const uint32_t op = lower >> 25;
        if (op == 0x01) { vu.stallOn(fs(), dest()); return; }   // SQ
        if (op != 0x40 || (lower & 0x3C) != 0x3C) return;
        const uint32_t e = ext2(lower);
        switch (e) {
        case 0x30: case 0x31: case 0x35: case 0x37: vu.stallOn(fs(), e == 0x31 ? 0xF : dest()); break;   // MOVE MR32 SQI SQD
        case 0x3C: case 0x42: case 0x43: vu.stallOn(fs(), fieldMask(fsf())); break;   // MTIR RINIT RXOR
        case 0x38: case 0x3A:   // DIV RSQRT
            vu.stallOn(fs(), fieldMask(fsf()));
            vu.stallOn(ft(), fieldMask(ftf()));
            if (vu.fdiv.active) vu.cycle = std::max(vu.cycle, vu.fdiv.ready);
            break;
        case 0x39:   // SQRT
            vu.stallOn(ft(), fieldMask(ftf()));
            if (vu.fdiv.active) vu.cycle = std::max(vu.cycle, vu.fdiv.ready);
            break;
        case 0x3B: if (vu.fdiv.active) vu.cycle = std::max(vu.cycle, vu.fdiv.ready); break;   // WAITQ
        case 0x7B: if (vu.efu.active) vu.cycle = std::max(vu.cycle, vu.efu.ready); break;     // WAITP
        default:
            if (e >= 0x70 && e <= 0x7E) {
                vu.stallOn(fs(), (e == 0x76) ? 0xF : (e >= 0x78) ? fieldMask(fsf()) : 0xE);
                if (vu.efu.active) vu.cycle = std::max(vu.cycle, vu.efu.ready - 1);
            }
            break;
        }
    }

    void loadVf(uint32_t reg, uint32_t mask, const uint32_t* source) {
        if (!reg) return;
        for (uint32_t c = 0; c < 4; ++c)
            if (mask & fieldMask(c)) { vu.vf[reg].u[c] = source[c]; vu.vfReady[reg][c] = vu.cycle + 4; }
    }
    void store(uint32_t* target, uint32_t mask, const VuVector& v) {
        for (uint32_t c = 0; c < 4; ++c) if (mask & fieldMask(c)) target[c] = v.u[c];
    }
    uint32_t wordIndex(uint32_t mask) const {
        return (mask & 8) ? 0 : (mask & 4) ? 1 : (mask & 2) ? 2 : 3;
    }
    void setVi(uint32_t reg, uint16_t value) { if (reg) vu.vi[reg] = value; }

    void lower(uint32_t lower) {
        code = lower;
        const uint32_t op = lower >> 25;
        const uint32_t d = dest();
        switch (op) {
        case 0x00: { const uint32_t a = static_cast<uint16_t>(vu.vi[is()] + imm11()); loadVf(ft(), d, vu.mem(a)); return; }
        case 0x01: { const uint32_t a = static_cast<uint16_t>(vu.vi[it()] + imm11()); store(vu.mem(a), d, vu.vf[fs()]); return; }
        case 0x04: {   // ILW
            const uint32_t a = static_cast<uint16_t>(vu.vi[is()] + imm11());
            setVi(it(), static_cast<uint16_t>(vu.mem(a)[wordIndex(d)]));
            return;
        }
        case 0x05: {   // ISW
            const uint32_t a = static_cast<uint16_t>(vu.vi[is()] + imm11());
            uint32_t* m = vu.mem(a);
            for (uint32_t c = 0; c < 4; ++c) if (d & fieldMask(c)) m[c] = vu.vi[it()];
            return;
        }
        case 0x08: case 0x09: {   // IADDIU, ISUBIU
            const uint16_t imm = static_cast<uint16_t>(((lower >> 10) & 0x7800) | (lower & 0x7FF));
            vu.writeVi(it(), static_cast<uint16_t>(op == 0x08 ? vu.vi[is()] + imm : vu.vi[is()] - imm));
            return;
        }
        case 0x10: vu.vi[1] = (vu.clip & 0xFFFFFF) == (lower & 0xFFFFFF); return;   // FCEQ
        case 0x11: vu.clipInternal = lower & 0xFFFFFF; return;                       // FCSET
        case 0x12: vu.vi[1] = (vu.clip & lower & 0xFFFFFF) != 0; return;            // FCAND
        case 0x13: vu.vi[1] = ((vu.clip | lower) & 0xFFFFFF) == 0xFFFFFF; return;    // FCOR
        case 0x14: case 0x15: case 0x16: case 0x17: {
            const uint32_t imm = (((lower >> 21) & 1) << 11) | (lower & 0x7FF);
            const uint32_t st = vu.status & 0xFFF;
            if (op == 0x15) { vu.statusInternal = (imm & 0xFC0) | (vu.statusInternal & 0x3F); return; }   // FSSET
            setVi(it(), static_cast<uint16_t>(op == 0x14 ? (st == imm) : op == 0x16 ? (st & imm) : (st | imm)));
            return;
        }
        case 0x18: setVi(it(), (vu.mac & 0xFFFF) == vu.vi[is()]); return;                         // FMEQ
        case 0x1A: setVi(it(), static_cast<uint16_t>(vu.mac & vu.vi[is()])); return;              // FMAND
        case 0x1B: setVi(it(), static_cast<uint16_t>((vu.mac | vu.vi[is()]) & 0xFFFF)); return;   // FMOR
        case 0x1C: setVi(it(), static_cast<uint16_t>(vu.clip & 0xFFF)); return;                    // FCGET
        case 0x20: vu.setBranch(branchTarget()); return;                                           // B
        case 0x21: setVi(it(), static_cast<uint16_t>(vu.tpc + 1)); vu.setBranch(branchTarget()); return;   // BAL
        case 0x24: vu.setBranch(vu.branchVi(is()) & 0x7FF); return;                                // JR
        case 0x25: {   // JALR
            const uint32_t target = vu.branchVi(is()) & 0x7FF;
            setVi(it(), static_cast<uint16_t>(vu.tpc + 1));
            vu.setBranch(target);
            return;
        }
        case 0x28: if (vu.branchVi(it()) == vu.branchVi(is())) vu.setBranch(branchTarget()); return;   // IBEQ
        case 0x29: if (vu.branchVi(it()) != vu.branchVi(is())) vu.setBranch(branchTarget()); return;   // IBNE
        case 0x2C: if (static_cast<int16_t>(vu.branchVi(is())) < 0) vu.setBranch(branchTarget()); return;
        case 0x2D: if (static_cast<int16_t>(vu.branchVi(is())) > 0) vu.setBranch(branchTarget()); return;
        case 0x2E: if (static_cast<int16_t>(vu.branchVi(is())) <= 0) vu.setBranch(branchTarget()); return;
        case 0x2F: if (static_cast<int16_t>(vu.branchVi(is())) >= 0) vu.setBranch(branchTarget()); return;
        case 0x40: special(lower); return;
        default: return;
        }
    }

    uint32_t branchTarget() const { return static_cast<uint32_t>(static_cast<int32_t>(vu.tpc) + imm11()) & 0x7FF; }

    void special(uint32_t lower) {
        const uint32_t low = lower & 0x3F;
        switch (low) {
        case 0x30: vu.writeVi(id(), static_cast<uint16_t>(vu.vi[is()] + vu.vi[it()])); return;   // IADD
        case 0x31: vu.writeVi(id(), static_cast<uint16_t>(vu.vi[is()] - vu.vi[it()])); return;   // ISUB
        case 0x32: {   // IADDI
            const int16_t imm = static_cast<int16_t>(((lower >> 6) & 0x10) ? static_cast<int>((lower >> 6) & 0xF) - 16 : static_cast<int>((lower >> 6) & 0xF));
            vu.writeVi(it(), static_cast<uint16_t>(vu.vi[is()] + imm));
            return;
        }
        case 0x34: vu.writeVi(id(), static_cast<uint16_t>(vu.vi[is()] & vu.vi[it()])); return;   // IAND
        case 0x35: vu.writeVi(id(), static_cast<uint16_t>(vu.vi[is()] | vu.vi[it()])); return;   // IOR
        default: break;
        }
        if ((low & 0x3C) != 0x3C) return;
        const uint32_t e = ext2(lower);
        const uint32_t d = dest();
        const VuVector& s = vu.vf[fs()];
        switch (e) {
        case 0x30: loadVf(ft(), d, s.u); return;   // MOVE
        case 0x31: { const uint32_t r[4] = {s.u[1], s.u[2], s.u[3], s.u[0]}; loadVf(ft(), d, r); return; }   // MR32
        case 0x34: {   // LQI
            if (ft()) loadVf(ft(), d, vu.mem(vu.vi[is()]));
            if (is()) vu.writeVi(is(), static_cast<uint16_t>(vu.vi[is()] + 1));
            return;
        }
        case 0x35: {   // SQI
            store(vu.mem(vu.vi[it()]), d, s);
            if (it()) vu.writeVi(it(), static_cast<uint16_t>(vu.vi[it()] + 1));
            return;
        }
        case 0x36: {   // LQD
            if (is()) vu.writeVi(is(), static_cast<uint16_t>(vu.vi[is()] - 1));
            if (ft()) loadVf(ft(), d, vu.mem(vu.vi[is()]));
            return;
        }
        case 0x37: {   // SQD
            if (it()) vu.writeVi(it(), static_cast<uint16_t>(vu.vi[it()] - 1));
            store(vu.mem(vu.vi[it()]), d, s);
            return;
        }
        case 0x38: case 0x39: case 0x3A: fdivOp(e); return;
        case 0x3B: return;   // WAITQ (the stall was taken)
        case 0x3C: vu.writeVi(it(), static_cast<uint16_t>(s.u[fsf()])); return;   // MTIR
        case 0x3D: {   // MFIR
            const uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(vu.vi[is()])));
            const uint32_t r[4] = {v, v, v, v};
            loadVf(ft(), d, r);
            return;
        }
        case 0x3E: setVi(it(), static_cast<uint16_t>(vu.mem(vu.vi[is()])[wordIndex(d)])); return;   // ILWR
        case 0x3F: {   // ISWR
            uint32_t* m = vu.mem(vu.vi[is()]);
            for (uint32_t c = 0; c < 4; ++c) if (d & fieldMask(c)) m[c] = vu.vi[it()];
            return;
        }
        case 0x40: {   // RNEXT
            const uint32_t x = (vu.r >> 4) & 1, y = (vu.r >> 22) & 1;
            vu.r = (((vu.r << 1) ^ x ^ y) & 0x7FFFFF) | 0x3F800000u;
            const uint32_t r[4] = {vu.r, vu.r, vu.r, vu.r};
            loadVf(ft(), d, r);
            return;
        }
        case 0x41: { const uint32_t r[4] = {vu.r, vu.r, vu.r, vu.r}; loadVf(ft(), d, r); return; }   // RGET
        case 0x42: vu.r = 0x3F800000u | (s.u[fsf()] & 0x7FFFFF); return;                                 // RINIT
        case 0x43: vu.r = 0x3F800000u | ((vu.r ^ s.u[fsf()]) & 0x7FFFFF); return;                        // RXOR
        case 0x64: { const uint32_t r[4] = {vu.p, vu.p, vu.p, vu.p}; loadVf(ft(), d, r); return; }   // MFP
        case 0x68: setVi(it(), static_cast<uint16_t>(vu.top)); return;    // XTOP
        case 0x69: setVi(it(), static_cast<uint16_t>(vu.itop)); return;   // XITOP
        case 0x6C: vu.xgkick(vu.vi[is()]); return;                        // XGKICK
        case 0x7B: return;                                                // WAITP
        default:
            if (e >= 0x70 && e <= 0x7E) efuOp(e);
            return;
        }
    }

    void fdivOp(uint32_t e) {
        const uint32_t tBits = vu.vf[ft()].u[ftf()], sBits = vu.vf[fs()].u[fsf()];
        const double t = vuIn(tBits), s = vuIn(sBits);
        uint32_t result = 0, flags = 0;
        const bool signsDiffer = ((tBits ^ sBits) & 0x80000000u) != 0;
        if (e == 0x38) {   // DIV
            if (t == 0.0) {
                flags = (s == 0.0) ? 0x10 : 0x20;
                result = signsDiffer ? 0xFF7FFFFFu : 0x7F7FFFFFu;
            } else {
                result = vuOut(s / t);
            }
        } else if (e == 0x39) {   // SQRT
            if (t < 0.0) flags = 0x10;
            result = vuOut(std::sqrt(std::fabs(t)));
        } else {   // RSQRT
            if (t == 0.0) {
                if (s != 0.0) { flags = 0x20; result = signsDiffer ? 0xFF7FFFFFu : 0x7F7FFFFFu; }
                else { flags = 0x30; result = signsDiffer ? 0x80000000u : 0; }
            } else {
                if (t < 0.0) flags = 0x10;
                result = vuOut(s / std::sqrt(std::fabs(t)));
            }
        }
        vu.fdiv.active = true;
        vu.fdiv.ready = vu.cycle + (e == 0x3A ? 13 : 7);
        vu.fdiv.value = result;
        vu.fdiv.flags = flags;
    }

    void efuOp(uint32_t e) {
        const VuVector& s = vu.vf[fs()];
        const double x = vuIn(s.u[0]), y = vuIn(s.u[1]), z = vuIn(s.u[2]), w = vuIn(s.u[3]);
        const double f = vuIn(s.u[fsf()]);
        double r = 0.0;
        switch (e) {
        case 0x70: r = x * x + y * y + z * z; break;
        case 0x71: r = 1.0 / (x * x + y * y + z * z); break;
        case 0x72: r = std::sqrt(x * x + y * y + z * z); break;
        case 0x73: r = 1.0 / std::sqrt(x * x + y * y + z * z); break;
        case 0x74: r = std::atan2(y, x); break;
        case 0x75: r = std::atan2(z, x); break;
        case 0x76: r = x + y + z + w; break;
        case 0x78: r = std::sqrt(std::fabs(f)); break;
        case 0x79: r = 1.0 / std::sqrt(std::fabs(f)); break;
        case 0x7A: r = 1.0 / f; break;
        case 0x7C: r = std::sin(f); break;
        case 0x7D: r = std::atan(f); break;
        case 0x7E: r = std::exp(-f); break;
        default: return;
        }
        vu.efu.active = true;
        vu.efu.ready = vu.cycle + static_cast<uint64_t>(efuLatency(e));
        vu.efu.value = vuOut(r);
        vu.efu.flags = 0;
    }
};

Vu1::Vu1() { reset(); }

void Vu1::reset() {
    std::memset(micro, 0, sizeof(micro));
    std::memset(data, 0, sizeof(data));
    std::memset(vf, 0, sizeof(vf));
    vf[0].f[3] = 1.0f;
    acc = {};
    std::memset(vi, 0, sizeof(vi));
    i = q = p = 0;
    r = 0x3F800000u;
    mac = status = clip = 0;
    macInternal = statusInternal = clipInternal = 0;
    fmacCount = 0;
    std::memset(vfReady, 0, sizeof(vfReady));
    fdiv = {};
    efu = {};
    cycle = 0;
    tpc = 0;
    top = itop = 0;
    instructions = 0;
    viBackupCycles = 0;
    branchPending = 0;
    delayBranch = false;
}

void Vu1::stallOn(uint32_t reg, uint32_t mask) {
    if (reg == 0 || reg == kAcc) return;   // ACC is forwarded: MULA/MADDA chains do not stall
    for (uint32_t c = 0; c < 4; ++c)
        if ((mask & (8u >> c)) && vfReady[reg][c] > cycle) cycle = vfReady[reg][c];
}

void Vu1::flushPipes() {
    // FMAC flag results become visible 4 cycles after issue, in order.
    uint32_t done = 0;
    while (done < fmacCount && fmac[done].ready <= cycle) {
        const FmacEntry& e = fmac[done];
        mac = e.mac & 0xFFFF;
        if (e.statusSet) status = (status & 0x30) | (e.status & 0xFC0) | (e.status & 0xF);
        else status = (status & 0xFF0) | (e.status & 0xF) | ((e.status & 0xF) << 6);
        if (e.clipSet) clip = e.clip;
        ++done;
    }
    if (done) {
        for (uint32_t k = done; k < fmacCount; ++k) fmac[k - done] = fmac[k];
        fmacCount -= done;
    }
    if (fdiv.active && fdiv.ready <= cycle) {
        fdiv.active = false;
        q = fdiv.value;
        status = (status & ~0x30u) | fdiv.flags | (fdiv.flags << 6);
    }
    if (efu.active && efu.ready <= cycle) {
        efu.active = false;
        p = efu.value;
    }
}

void Vu1::flushAll() {
    for (uint32_t k = 0; k < fmacCount; ++k) cycle = std::max(cycle, fmac[k].ready);
    if (fdiv.active) cycle = std::max(cycle, fdiv.ready);
    if (efu.active) cycle = std::max(cycle, efu.ready);
    flushPipes();
}

void Vu1::writeVi(uint32_t reg, uint16_t value) {
    if (!reg) return;
    if (!(viBackupCycles && reg == viBackupReg)) {
        viBackupReg = reg;
        viBackupValue = vi[reg];
    }
    viBackupCycles = 2;
    vi[reg] = value;
}

uint16_t Vu1::branchVi(uint32_t reg) const {
    if (viBackupCycles > 0 && reg == viBackupReg) return viBackupValue;
    return vi[reg];
}

void Vu1::setBranch(uint32_t target) {
    if (branchPending == 1) {   // a branch in a branch delay slot
        delayBranch = true;
        delayTarget = target;
    } else {
        branchPending = 2;
        branchTarget = target;
    }
}

void Vu1::xgkick(uint32_t address) {
    if (!gifSink) return;
    std::vector<uint8_t> packet;
    uint32_t qw = address & 0x3FF;
    auto append = [&](uint32_t count) {
        for (uint32_t k = 0; k < count; ++k) {
            const uint8_t* source = data + (qw & 0x3FF) * 16;
            packet.insert(packet.end(), source, source + 16);
            ++qw;
        }
    };
    for (uint32_t total = 0; total < 1024;) {
        uint64_t lo = 0;
        std::memcpy(&lo, data + (qw & 0x3FF) * 16, 8);
        append(1);
        const uint32_t nloop = static_cast<uint32_t>(lo & 0x7FFF);
        const bool eop = (lo >> 15) & 1;
        const uint32_t flg = static_cast<uint32_t>(lo >> 58) & 3;
        uint32_t nreg = static_cast<uint32_t>(lo >> 60) & 0xF;
        if (!nreg) nreg = 16;
        const uint32_t count = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
        append(std::min(count, 1024u));
        total += 1 + count;
        if (eop) break;
    }
    gifSink->gif(1, packet.data(), packet.size());
}

uint64_t Vu1::run(uint32_t startPc, uint64_t maxCycles) {
    VuExec exec{*this};
    tpc = startPc & 0x7FF;
    stoppedRunaway = false;
    const uint64_t begin = cycle;
    int ebit = 0;
    for (;;) {
        if (cycle - begin > maxCycles) {
            stoppedRunaway = true;
            break;
        }
        const uint64_t before = cycle;
        ++cycle;
        uint32_t lowerWord = 0, upperWord = 0;
        std::memcpy(&lowerWord, micro + tpc * 8, 4);
        std::memcpy(&upperWord, micro + tpc * 8 + 4, 4);
        if (coverage) coverage[tpc] = 1;
        tpc = (tpc + 1) & 0x7FF;
        ++instructions;
        if (upperWord & 0x40000000u) ebit = 2;
        const bool iBit = (upperWord & 0x80000000u) != 0;

        exec.upperStalls(upperWord);
        if (!iBit) exec.lowerStalls(lowerWord);
        if (viBackupCycles > 0)
            viBackupCycles -= static_cast<int>(std::min<uint64_t>(cycle - before, static_cast<uint64_t>(viBackupCycles)));
        flushPipes();
        if (trace) std::fprintf(trace, "%04x c%llu clip %06x mac %04x vi1 %04x\n", ((tpc - 1) & 0x7FF) * 8, static_cast<unsigned long long>(cycle - begin), clip, mac, vi[1]);

        UpperResult up = exec.upper(upperWord);
        bool fsset = false, fcset = false;
        if (iBit) {
            i = lowerWord;
        } else {
            const VuExec::LowerInfo info = exec.lowerInfo(lowerWord);
            // When both slots write the same VF register, the lower instruction is discarded.
            const bool discard = up.reg >= 0 && up.reg < 32 && info.vfWrite == up.reg;
            if (!discard) {
                exec.lower(lowerWord);
                fsset = info.fsset;
                fcset = info.fcset;
            }
        }
        // Commit the upper result.
        if (up.reg >= 0) {
            VuVector& target = up.reg == static_cast<int>(kAcc) ? acc : vf[up.reg];
            for (uint32_t c = 0; c < 4; ++c)
                if (up.mask & (8u >> c)) {
                    target.u[c] = up.value.u[c];
                    vfReady[up.reg][c] = cycle + 4;
                }
        }
        if (up.flags || up.clipOp || fsset || fcset) {
            if (fmacCount == 8) {   // cannot happen with a 4-cycle latency; keep the newest
                for (uint32_t k = 1; k < 8; ++k) fmac[k - 1] = fmac[k];
                --fmacCount;
            }
            fmac[fmacCount++] = {cycle + 4, macInternal, statusInternal, clipInternal, fsset, up.clipOp || fcset};
        }
        if (branchPending > 0 && branchPending-- == 1) {
            tpc = branchTarget;
            if (delayBranch) {
                branchPending = 1;
                branchTarget = delayTarget;
                delayBranch = false;
            }
        }
        if (ebit > 0 && ebit-- == 1) {
            flushAll();
            viBackupCycles = 0;
            branchPending = 0;
            break;
        }
    }
    vf[0].u[0] = vf[0].u[1] = vf[0].u[2] = 0;
    vf[0].f[3] = 1.0f;
    vi[0] = 0;
    return cycle - begin;
}

}  // namespace gt4::ps2
