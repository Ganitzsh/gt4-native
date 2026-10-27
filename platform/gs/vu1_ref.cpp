// Reference VU1 interpreter (GT4_VU1_DIFF only); adapted from ps2-web-recomp, MIT, see vu1_ref.h.
#include "vu1_ref.h"

#include "vu_float.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

namespace gt4::ps2 {
namespace {

namespace fp = vufp;   // exact VU float arithmetic (vu_float.h)

using Reg = VuVector;

struct FlagPipe {
    uint32_t t = 16;
    uint32_t mac_ts[8] = {}, mac_v[8] = {}, status_v[8] = {}, clip_ts[8] = {}, clip_v[8] = {};
    unsigned mh = 0, ch = 0;

    void start(const Vu1Ref& r) {
        for (int i = 0; i < 8; ++i) {
            mac_ts[i] = clip_ts[i] = 0;
            mac_v[i] = r.mac;
            status_v[i] = r.status;
            clip_v[i] = r.clip;
        }
    }
    void set_mac(uint32_t mac, uint32_t status) {
        const unsigned i = mh++ & 7u;
        mac_ts[i] = t;
        mac_v[i] = mac;
        status_v[i] = status;
    }
    void set_clip(uint32_t clip, uint32_t at) {
        const unsigned i = ch++ & 7u;
        clip_ts[i] = at;
        clip_v[i] = clip;
    }
    void force_mac(uint32_t mac, uint32_t status) {
        set_mac(mac, status);
        mac_ts[(mh - 1) & 7u] = t - 4u;
    }
    unsigned mac_slot() const {
        for (unsigned k = 1; k <= 8; ++k) {
            const unsigned i = (mh - k) & 7u;
            if (mac_ts[i] + 4u <= t) return i;
        }
        return (mh - 8) & 7u;
    }
    uint32_t mac() const { return mac_v[mac_slot()]; }
    uint32_t status() const { return status_v[mac_slot()]; }
    uint32_t clip() const {
        for (unsigned k = 1; k <= 8; ++k) {
            const unsigned i = (ch - k) & 7u;
            if (clip_ts[i] + 3u <= t) return clip_v[i];
        }
        return clip_v[(ch - 8) & 7u];
    }
};

struct UpperOut {
    int target = -1;
    Reg value{};
    unsigned dest = 0;
    bool flags = false;
    bool has_clip = false;
    uint32_t clip = 0;
};

constexpr int kAcc = 32;

void set_flags(Vu1Ref& r, const Reg& v, unsigned dest) {
    uint32_t mac = 0;
    for (int i = 0; i < 4; ++i) {
        if (!(dest & (8u >> i))) continue;
        const uint32_t bit = 3u - static_cast<uint32_t>(i);
        const uint32_t x = v.u[i];
        if (!(x & 0x7F800000u)) mac |= 1u << bit;
        if (x & 0x80000000u) mac |= 1u << (bit + 4);
        if ((x & 0x7FFFFFFFu) == 0x7FFFFFFFu) mac |= 1u << (bit + 12);
    }
    r.mac = mac;
    uint32_t status = r.status & 0xFF0u;
    if (mac & 0x000Fu) status |= 0x1u;
    if (mac & 0x00F0u) status |= 0x2u;
    if (mac & 0x0F00u) status |= 0x4u;
    if (mac & 0xF000u) status |= 0x8u;
    status |= (status & 0xFu) << 6;
    r.status = (status & ~0x30u) | (r.status & 0x30u);
}

uint32_t clip_value(const Vu1Ref& r, const Reg& s, uint32_t w) {
    const int32_t lim = fp::key(w & fp::kAbs);
    uint32_t f = 0;
    for (int i = 0; i < 3; ++i) {
        const int32_t x = fp::key(s.u[i]);
        if (x > lim) f |= 1u << (i * 2);
        if (x < -lim) f |= 2u << (i * 2);
    }
    return ((r.clip << 6) | f) & 0xFFFFFFu;
}

template <typename F>
UpperOut lanes(int target, unsigned dest, bool flags, F f) {
    UpperOut o;
    o.target = target;
    o.dest = dest;
    o.flags = flags;
    for (int i = 0; i < 4; ++i)
        if (dest & (8u >> i)) o.value.u[i] = f(i);
    return o;
}

UpperOut upper(Vu1Ref& r, uint32_t w) {
    const unsigned dest = (w >> 21) & 15u, ft = (w >> 16) & 31u, fs = (w >> 11) & 31u, fd = (w >> 6) & 31u, bc = w & 3u;
    const Reg s = r.vf[fs], t = r.vf[ft], acc = r.acc;
    const uint32_t b = t.u[bc];
    const unsigned funct = w & 63u;
    if (funct < 0x3C) {
        switch (funct) {
        case 0x00: case 0x01: case 0x02: case 0x03: return lanes(fd, dest, true, [&](int i) { return fp::add(s.u[i], b); });
        case 0x04: case 0x05: case 0x06: case 0x07: return lanes(fd, dest, true, [&](int i) { return fp::sub(s.u[i], b); });
        case 0x08: case 0x09: case 0x0A: case 0x0B: return lanes(fd, dest, true, [&](int i) { return fp::add(acc.u[i], fp::mul(s.u[i], b)); });
        case 0x0C: case 0x0D: case 0x0E: case 0x0F: return lanes(fd, dest, true, [&](int i) { return fp::sub(acc.u[i], fp::mul(s.u[i], b)); });
        case 0x10: case 0x11: case 0x12: case 0x13: return lanes(fd, dest, false, [&](int i) { return fp::max(s.u[i], b); });
        case 0x14: case 0x15: case 0x16: case 0x17: return lanes(fd, dest, false, [&](int i) { return fp::min(s.u[i], b); });
        case 0x18: case 0x19: case 0x1A: case 0x1B: return lanes(fd, dest, true, [&](int i) { return fp::mul(s.u[i], b); });
        case 0x1C: return lanes(fd, dest, true, [&](int i) { return fp::mul(s.u[i], r.q); });
        case 0x1D: return lanes(fd, dest, false, [&](int i) { return fp::max(s.u[i], r.i); });
        case 0x1E: return lanes(fd, dest, true, [&](int i) { return fp::mul(s.u[i], r.i); });
        case 0x1F: return lanes(fd, dest, false, [&](int i) { return fp::min(s.u[i], r.i); });
        case 0x20: return lanes(fd, dest, true, [&](int i) { return fp::add(s.u[i], r.q); });
        case 0x21: return lanes(fd, dest, true, [&](int i) { return fp::add(acc.u[i], fp::mul(s.u[i], r.q)); });
        case 0x22: return lanes(fd, dest, true, [&](int i) { return fp::add(s.u[i], r.i); });
        case 0x23: return lanes(fd, dest, true, [&](int i) { return fp::add(acc.u[i], fp::mul(s.u[i], r.i)); });
        case 0x24: return lanes(fd, dest, true, [&](int i) { return fp::sub(s.u[i], r.q); });
        case 0x25: return lanes(fd, dest, true, [&](int i) { return fp::sub(acc.u[i], fp::mul(s.u[i], r.q)); });
        case 0x26: return lanes(fd, dest, true, [&](int i) { return fp::sub(s.u[i], r.i); });
        case 0x27: return lanes(fd, dest, true, [&](int i) { return fp::sub(acc.u[i], fp::mul(s.u[i], r.i)); });
        case 0x28: return lanes(fd, dest, true, [&](int i) { return fp::add(s.u[i], t.u[i]); });
        case 0x29: return lanes(fd, dest, true, [&](int i) { return fp::add(acc.u[i], fp::mul(s.u[i], t.u[i])); });
        case 0x2A: return lanes(fd, dest, true, [&](int i) { return fp::mul(s.u[i], t.u[i]); });
        case 0x2B: return lanes(fd, dest, false, [&](int i) { return fp::max(s.u[i], t.u[i]); });
        case 0x2C: return lanes(fd, dest, true, [&](int i) { return fp::sub(s.u[i], t.u[i]); });
        case 0x2D: return lanes(fd, dest, true, [&](int i) { return fp::sub(acc.u[i], fp::mul(s.u[i], t.u[i])); });
        case 0x2E: {
            UpperOut o;
            o.target = static_cast<int>(fd);
            o.dest = 0xEu;
            o.flags = true;
            o.value.u[0] = fp::sub(acc.u[0], fp::mul(s.u[1], t.u[2]));
            o.value.u[1] = fp::sub(acc.u[1], fp::mul(s.u[2], t.u[0]));
            o.value.u[2] = fp::sub(acc.u[2], fp::mul(s.u[0], t.u[1]));
            return o;
        }
        case 0x2F: return lanes(fd, dest, false, [&](int i) { return fp::min(s.u[i], t.u[i]); });
        default: return UpperOut{};
        }
    }
    const unsigned index = ((w >> 6) & 31u) * 4u + (funct & 3u);
    static const int kFrac[4] = {0, 4, 12, 15};
    switch (index) {
    case 0x00: case 0x01: case 0x02: case 0x03: return lanes(kAcc, dest, true, [&](int i) { return fp::add(s.u[i], b); });
    case 0x04: case 0x05: case 0x06: case 0x07: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(s.u[i], b); });
    case 0x08: case 0x09: case 0x0A: case 0x0B: return lanes(kAcc, dest, true, [&](int i) { return fp::add(acc.u[i], fp::mul(s.u[i], b)); });
    case 0x0C: case 0x0D: case 0x0E: case 0x0F: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(acc.u[i], fp::mul(s.u[i], b)); });
    case 0x10: case 0x11: case 0x12: case 0x13: return lanes(ft, dest, false, [&](int i) { return fp::from_fixed(s.s[i], kFrac[index & 3u]); });
    case 0x14: case 0x15: case 0x16: case 0x17: return lanes(ft, dest, false, [&](int i) { return fp::to_fixed(s.u[i], kFrac[index & 3u]); });
    case 0x18: case 0x19: case 0x1A: case 0x1B: return lanes(kAcc, dest, true, [&](int i) { return fp::mul(s.u[i], b); });
    case 0x1C: return lanes(kAcc, dest, true, [&](int i) { return fp::mul(s.u[i], r.q); });
    case 0x1D: return lanes(ft, dest, false, [&](int i) { return s.u[i] & fp::kAbs; });
    case 0x1E: return lanes(kAcc, dest, true, [&](int i) { return fp::mul(s.u[i], r.i); });
    case 0x1F: {
        UpperOut o;
        o.has_clip = true;
        o.clip = clip_value(r, s, t.u[3]);
        return o;
    }
    case 0x20: return lanes(kAcc, dest, true, [&](int i) { return fp::add(s.u[i], r.q); });
    case 0x21: return lanes(kAcc, dest, true, [&](int i) { return fp::add(acc.u[i], fp::mul(s.u[i], r.q)); });
    case 0x22: return lanes(kAcc, dest, true, [&](int i) { return fp::add(s.u[i], r.i); });
    case 0x23: return lanes(kAcc, dest, true, [&](int i) { return fp::add(acc.u[i], fp::mul(s.u[i], r.i)); });
    case 0x24: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(s.u[i], r.q); });
    case 0x25: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(acc.u[i], fp::mul(s.u[i], r.q)); });
    case 0x26: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(s.u[i], r.i); });
    case 0x27: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(acc.u[i], fp::mul(s.u[i], r.i)); });
    case 0x28: return lanes(kAcc, dest, true, [&](int i) { return fp::add(s.u[i], t.u[i]); });
    case 0x29: return lanes(kAcc, dest, true, [&](int i) { return fp::add(acc.u[i], fp::mul(s.u[i], t.u[i])); });
    case 0x2A: return lanes(kAcc, dest, true, [&](int i) { return fp::mul(s.u[i], t.u[i]); });
    case 0x2C: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(s.u[i], t.u[i]); });
    case 0x2D: return lanes(kAcc, dest, true, [&](int i) { return fp::sub(acc.u[i], fp::mul(s.u[i], t.u[i])); });
    case 0x2E: {
        UpperOut o;
        o.target = kAcc;
        o.dest = 0xEu;
        o.flags = true;
        o.value.u[0] = fp::mul(s.u[1], t.u[2]);
        o.value.u[1] = fp::mul(s.u[2], t.u[0]);
        o.value.u[2] = fp::mul(s.u[0], t.u[1]);
        return o;
    }
    default: return UpperOut{};
    }
}

void commit(Vu1Ref& r, const UpperOut& o) {
    if (o.target < 0) return;
    if (o.target > 0) {
        Reg& dst = o.target == kAcc ? r.acc : r.vf[o.target];
        for (int i = 0; i < 4; ++i)
            if (o.dest & (8u >> i)) dst.u[i] = o.value.u[i];
    }
    if (o.flags) set_flags(r, o.value, o.dest);
}

uint32_t f2u(float f) { return std::bit_cast<uint32_t>(f); }
float u2f(uint32_t v) {
    if (!(v & 0x7F800000u)) v &= 0x80000000u;
    if ((v & 0x7F800000u) == 0x7F800000u) v = (v & 0x80000000u) | 0x7F7FFFFFu;
    return std::bit_cast<float>(v);
}

uint32_t rand_next(uint32_t r) {
    const uint32_t bit = ((r >> 4) ^ (r >> 22)) & 1u;
    return ((r << 1) | bit) & 0x7FFFFFu;
}

struct Machine {
    Vu1Ref& r;
    FlagPipe pipe;

    uint8_t* qword(uint32_t index) { return r.data + (index & 1023u) * 16; }
    void store_vi(unsigned index, uint32_t value) {
        if (index) r.vi[index] = static_cast<uint16_t>(value);
    }

    void xgkick(uint32_t address) {
        const size_t start = r.kicks.size();
        kickPacket(address);
        if (r.sink) r.sink->gif(1, r.kicks.data() + start, r.kicks.size() - start);
    }

    void kickPacket(uint32_t address) {
        uint32_t q = address & 1023u, count = 0;
        bool done = false;
        while (!done && count < 1024u) {
            uint64_t tag;
            std::memcpy(&tag, qword(q), 8);
            r.kicks.insert(r.kicks.end(), qword(q), qword(q) + 16);
            ++q;
            ++count;
            const uint32_t nloop = static_cast<uint32_t>(tag) & 0x7FFFu;
            const bool eop = (tag >> 15) & 1u;
            const uint32_t flg = static_cast<uint32_t>(tag >> 58) & 3u;
            uint32_t nreg = static_cast<uint32_t>(tag >> 60) & 0xFu;
            if (!nreg) nreg = 16;
            const uint32_t body = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
            for (uint32_t i = 0; i < body && count < 1024u; ++i, ++q, ++count) r.kicks.insert(r.kicks.end(), qword(q), qword(q) + 16);
            done = eop;
        }
    }

    void div(uint32_t s, uint32_t t) {
        const fp::Result x = fp::div(s, t);
        r.q = x.value;
        r.status = (r.status & ~0x30u) | (x.invalid ? 0x20u : 0u);
    }
    void rsqrt(uint32_t s, uint32_t t) {
        const fp::Result x = fp::rsqrt(s, t);
        r.q = x.value;
        r.status = (r.status & ~0x30u) | (x.invalid ? 0x20u : 0u);
    }

    void lower(uint32_t w, uint32_t pc, uint32_t& npc, bool& branch) {
        const unsigned dest = (w >> 21) & 15u, it = (w >> 16) & 15u, is = (w >> 11) & 15u, id = (w >> 6) & 15u;
        const uint32_t ft = (w >> 16) & 31u, fs = (w >> 11) & 31u;
        const int32_t imm11 = static_cast<int32_t>(w << 21) >> 21;
        const auto branch_to = [&](uint32_t target) {
            npc = target & 16383u;
            branch = true;
        };
        if ((w >> 25) == 0x40u) {
            const unsigned funct = w & 63u;
            if (funct < 0x3C) {
                switch (funct) {
                case 0x30: store_vi(id, r.vi[is] + r.vi[it]); break;
                case 0x31: store_vi(id, r.vi[is] - r.vi[it]); break;
                case 0x32: store_vi(it, r.vi[is] + static_cast<uint32_t>(static_cast<int32_t>(((w >> 6) & 31u) << 27) >> 27)); break;
                case 0x34: store_vi(id, r.vi[is] & r.vi[it]); break;
                case 0x35: store_vi(id, r.vi[is] | r.vi[it]); break;
                default: break;
                }
                return;
            }
            const unsigned index = ((w >> 6) & 31u) * 4u + (funct & 3u);
            const unsigned fsf = (w >> 21) & 3u, ftf = (w >> 23) & 3u;
            switch (index) {
            case 0x30: if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].u[i] = r.vf[fs].u[i]; break;
            case 0x31: if (ft) { const Reg s = r.vf[fs]; for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].u[i] = s.u[(i + 1) & 3]; } break;
            case 0x34: {
                const uint8_t* q = qword(r.vi[is]);
                if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(&r.vf[ft].u[i], q + i * 4, 4);
                store_vi(is, r.vi[is] + 1u);
                break;
            }
            case 0x35: {
                uint8_t* q = qword(r.vi[it]);
                for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &r.vf[fs].u[i], 4);
                store_vi(it, r.vi[it] + 1u);
                break;
            }
            case 0x36: {
                store_vi(is, r.vi[is] - 1u);
                const uint8_t* q = qword(r.vi[is]);
                if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(&r.vf[ft].u[i], q + i * 4, 4);
                break;
            }
            case 0x37: {
                store_vi(it, r.vi[it] - 1u);
                uint8_t* q = qword(r.vi[it]);
                for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &r.vf[fs].u[i], 4);
                break;
            }
            case 0x38: div(r.vf[fs].u[fsf], r.vf[ft].u[ftf]); break;
            case 0x39: r.q = fp::sqrt(r.vf[ft].u[ftf]); break;
            case 0x3A: rsqrt(r.vf[fs].u[fsf], r.vf[ft].u[ftf]); break;
            case 0x3B: break;
            case 0x3C: store_vi(it, r.vf[fs].u[fsf]); break;
            case 0x3D: if (ft) { const uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(r.vi[is]))); for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].u[i] = v; } break;
            case 0x3E: {
                const uint8_t* q = qword(r.vi[is]);
                for (int i = 0; i < 4; ++i)
                    if (dest & (8u >> i)) {
                        uint16_t v;
                        std::memcpy(&v, q + i * 4, 2);
                        store_vi(it, v);
                        break;
                    }
                break;
            }
            case 0x3F: {
                uint8_t* q = qword(r.vi[is]);
                const uint32_t v = r.vi[it];
                for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &v, 4);
                break;
            }
            case 0x40:
                r.r = rand_next(r.r);
                if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].u[i] = r.r | fp::kOne;
                break;
            case 0x41: if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].u[i] = (r.r & 0x7FFFFFu) | fp::kOne; break;
            case 0x42: r.r = r.vf[fs].u[fsf] & 0x7FFFFFu; break;
            case 0x43: r.r = (r.r ^ r.vf[fs].u[fsf]) & 0x7FFFFFu; break;
            case 0x64: if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) r.vf[ft].u[i] = r.p; break;
            case 0x68: store_vi(it, r.top); break;
            case 0x69: store_vi(it, r.itop); break;
            case 0x6C: xgkick(r.vi[is]); break;
            case 0x70: case 0x71: case 0x72: case 0x73:
                r.p = fp::efu_length(r.vf[fs].u[0], r.vf[fs].u[1], r.vf[fs].u[2], static_cast<int>(index - 0x70));
                break;
            case 0x74: r.p = f2u(std::atan2(u2f(r.vf[fs].u[1]), u2f(r.vf[fs].u[0]))); break;
            case 0x75: r.p = f2u(std::atan2(u2f(r.vf[fs].u[2]), u2f(r.vf[fs].u[0]))); break;
            case 0x76: r.p = f2u(u2f(r.vf[fs].u[0]) + u2f(r.vf[fs].u[1]) + u2f(r.vf[fs].u[2]) + u2f(r.vf[fs].u[3])); break;
            case 0x78: r.p = f2u(std::sqrt(std::fabs(u2f(r.vf[fs].u[fsf])))); break;
            case 0x79: { const float v = std::sqrt(std::fabs(u2f(r.vf[fs].u[fsf]))); r.p = f2u(v != 0.0f ? 1.0f / v : 3.4e38f); break; }
            case 0x7A: { const float v = u2f(r.vf[fs].u[fsf]); r.p = f2u(v != 0.0f ? 1.0f / v : 3.4e38f); break; }
            case 0x7B: break;
            case 0x7C: r.p = f2u(std::sin(u2f(r.vf[fs].u[fsf]))); break;
            case 0x7D: r.p = f2u(std::atan(u2f(r.vf[fs].u[fsf]))); break;
            case 0x7E: r.p = f2u(std::exp(-u2f(r.vf[fs].u[fsf]))); break;
            default: break;
            }
            return;
        }
        const uint32_t target = pc + 8u + static_cast<uint32_t>(imm11) * 8u;
        switch (w >> 25) {
        case 0x00: {
            const uint8_t* q = qword(r.vi[is] + static_cast<uint32_t>(imm11));
            if (ft) for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(&r.vf[ft].u[i], q + i * 4, 4);
            break;
        }
        case 0x01: {
            uint8_t* q = qword(r.vi[it] + static_cast<uint32_t>(imm11));
            for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &r.vf[fs].u[i], 4);
            break;
        }
        case 0x04: {
            const uint8_t* q = qword(r.vi[is] + static_cast<uint32_t>(imm11));
            for (int i = 0; i < 4; ++i)
                if (dest & (8u >> i)) {
                    uint16_t v;
                    std::memcpy(&v, q + i * 4, 2);
                    store_vi(it, v);
                    break;
                }
            break;
        }
        case 0x05: {
            uint8_t* q = qword(r.vi[is] + static_cast<uint32_t>(imm11));
            const uint32_t v = r.vi[it];
            for (int i = 0; i < 4; ++i) if (dest & (8u >> i)) std::memcpy(q + i * 4, &v, 4);
            break;
        }
        case 0x08: store_vi(it, r.vi[is] + (((w >> 10) & 0x7800u) | (w & 0x7FFu))); break;
        case 0x09: store_vi(it, r.vi[is] - (((w >> 10) & 0x7800u) | (w & 0x7FFu))); break;
        case 0x10: store_vi(1, (pipe.clip() & 0xFFFFFFu) == (w & 0xFFFFFFu) ? 1u : 0u); break;
        case 0x11:
            r.clip = w & 0xFFFFFFu;
            pipe.set_clip(r.clip, pipe.t - 4u);
            break;
        case 0x12: store_vi(1, (pipe.clip() & w & 0xFFFFFFu) ? 1u : 0u); break;
        case 0x13: store_vi(1, ((pipe.clip() | w) & 0xFFFFFFu) == 0xFFFFFFu ? 1u : 0u); break;
        case 0x14: store_vi(it, (pipe.status() & 0xFFFu) == ((((w >> 10) & 0x800u) | (w & 0x7FFu))) ? 1u : 0u); break;
        case 0x15:
            r.status = (pipe.status() & 0x3Fu) | ((((w >> 10) & 0x800u) | (w & 0x7FFu)) & 0xFC0u);
            pipe.force_mac(r.mac, r.status);
            break;
        case 0x16: store_vi(it, pipe.status() & (((w >> 10) & 0x800u) | (w & 0x7FFu))); break;
        case 0x17: store_vi(it, ((pipe.status() | (((w >> 10) & 0x800u) | (w & 0x7FFu))) & 0xFFFu) == 0xFFFu ? 1u : 0u); break;
        case 0x18: store_vi(it, (pipe.mac() & 0xFFFFu) == r.vi[is] ? 1u : 0u); break;
        case 0x1A: store_vi(it, pipe.mac() & r.vi[is]); break;
        case 0x1B: store_vi(it, ((pipe.mac() | r.vi[is]) & 0xFFFFu) == 0xFFFFu ? 1u : 0u); break;
        case 0x1C: store_vi(it, pipe.clip() & 0xFFFu); break;
        case 0x20: branch_to(target); break;
        case 0x21: store_vi(it, (pc + 16u) / 8u); branch_to(target); break;
        case 0x24: branch_to(static_cast<uint32_t>(r.vi[is]) * 8u); break;
        case 0x25: { const uint32_t t = static_cast<uint32_t>(r.vi[is]) * 8u; store_vi(it, (pc + 16u) / 8u); branch_to(t); break; }
        case 0x28: if (r.vi[is] == r.vi[it]) branch_to(target); break;
        case 0x29: if (r.vi[is] != r.vi[it]) branch_to(target); break;
        case 0x2C: if (static_cast<int16_t>(r.vi[is]) < 0) branch_to(target); break;
        case 0x2D: if (static_cast<int16_t>(r.vi[is]) > 0) branch_to(target); break;
        case 0x2E: if (static_cast<int16_t>(r.vi[is]) <= 0) branch_to(target); break;
        case 0x2F: if (static_cast<int16_t>(r.vi[is]) >= 0) branch_to(target); break;
        default: break;
        }
    }
};

}  // namespace

void Vu1Ref::load(const Vu1& vu) {
    std::memcpy(vf, vu.vf, sizeof vf);
    acc = vu.acc;
    std::memcpy(vi, vu.vi, sizeof vi);
    q = vu.q;
    p = vu.p;
    i = vu.i;
    r = vu.r;
    mac = vu.mac;
    status = vu.status;
    clip = vu.clip;
    top = vu.top;
    itop = vu.itop;
    std::memcpy(micro, vu.micro, sizeof micro);
    std::memcpy(data, vu.data, sizeof data);
    kicks.clear();
    runaway = false;
}

void Vu1Ref::store(Vu1& vu) const {
    std::memcpy(vu.vf, vf, sizeof vf);
    vu.acc = acc;
    std::memcpy(vu.vi, vi, sizeof vi);
    vu.q = q;
    vu.p = p;
    vu.i = i;
    vu.r = r;
    vu.mac = vu.macInternal = mac;
    vu.status = vu.statusInternal = status;
    vu.clip = vu.clipInternal = clip;
    vu.tpc = tpc & 0x7FF;
    std::memcpy(vu.data, data, sizeof data);
}

void Vu1Ref::run(uint32_t address) {
    Machine m{*this, {}};
    m.pipe.start(*this);
    uint32_t pc = (address * 8) & 16383u, npc = (pc + 8u) & 16383u;
    bool ending = false;
    for (uint32_t steps = 0; steps < 1'000'000; ++steps) {
        uint32_t lo, hi;
        std::memcpy(&lo, micro + pc, 4);
        std::memcpy(&hi, micro + pc + 4, 4);
        const uint32_t current = pc;
        pc = npc;
        npc = (npc + 8u) & 16383u;
        ++m.pipe.t;
        if (trace) std::fprintf(trace, "%04x t%u clip %06x mac %04x vi1 %04x\n", current, m.pipe.t, m.pipe.clip(), m.pipe.mac(), vi[1]);
        if (trace) traceRegs(trace, vf, acc);
        const UpperOut out = upper(*this, hi);
        if (hi & 0x80000000u) {
            i = lo;
        } else {
            bool branch = false;
            uint32_t target = npc;
            m.lower(lo, current, target, branch);
            if (branch) npc = target;
        }
        commit(*this, out);
        if (out.target >= 0 && out.flags) m.pipe.set_mac(mac, status);
        if (out.has_clip) {
            clip = out.clip;
            m.pipe.set_clip(out.clip, m.pipe.t);
        }
        vf[0].u[0] = vf[0].u[1] = vf[0].u[2] = 0;
        vf[0].u[3] = fp::kOne;
        vi[0] = 0;
        if (ending) {
            tpc = pc / 8;
            return;
        }
        if (hi & 0x40000000u) ending = true;
    }
    runaway = true;
}

}  // namespace gt4::ps2
