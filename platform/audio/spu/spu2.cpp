// The SPU2 (spu2.h).
#include "spu2.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace gt4::spu {

namespace {

uint16_t rd16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

const int kFilter[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

// libsd's reverb presets (psx-spx "Reverb Examples"), in register order dAPF1 .. vRIN. Offsets are in 8-byte units
// (4 samples).
const uint16_t kReverbPresets[10][32] = {
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001,
     0x0001, 0x0001, 0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000, 0x0001, 0x0001,
     0x0001, 0x0001, 0x0000, 0x0000},  // off
    {0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80, 0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227,
     0x0374, 0x01EF, 0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x01B4, 0x0136,
     0x00B8, 0x005C, 0x8000, 0x8000},  // room
    {0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00, 0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF,
     0x0372, 0x0266, 0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7, 0x018F, 0x00B5, 0x00B4, 0x0080,
     0x004C, 0x0026, 0x8000, 0x8000},  // studio A (small)
    {0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0, 0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F,
     0x07A2, 0x0616, 0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7, 0x042F, 0x0265, 0x0264, 0x01B2,
     0x0100, 0x0080, 0x8000, 0x8000},  // studio B (medium)
    {0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680, 0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C,
     0x0BD9, 0x0973, 0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2, 0x05EA, 0x031D, 0x031C, 0x0238,
     0x0154, 0x00AA, 0x8000, 0x8000},  // studio C (large)
    {0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000, 0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD,
     0x11BC, 0x0DC1, 0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD, 0x09C2, 0x05C1, 0x05C0, 0x041A,
     0x0274, 0x013A, 0x8000, 0x8000},  // hall
    {0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000, 0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B,
     0x1BC2, 0x16B2, 0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D, 0x1056, 0x0AE1, 0x0AE0, 0x07A2,
     0x0464, 0x0232, 0x8000, 0x8000},  // space
    {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100, 0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005,
     0x0000, 0x0000, 0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002,
     0x0004, 0x0002, 0x8000, 0x8000},  // echo
    {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005,
     0x0000, 0x0000, 0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002,
     0x0004, 0x0002, 0x8000, 0x8000},  // delay
    {0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500, 0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF,
     0x02B0, 0x01D7, 0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059, 0x01A0, 0x00E3, 0x0058, 0x0040,
     0x0028, 0x0014, 0x8000, 0x8000},  // pipe (psx-spx "half echo")
};
const uint32_t kReverbBytes[10] = {0x10, 0x26C0, 0x1F40, 0x4840, 0x6FE0, 0xADE0, 0xF6C0, 0x18040, 0x18040, 0x3C00};

enum ReverbReg {
    dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2,
    mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1,
    mLAPF2, mRAPF2, vLIN, vRIN
};

int32_t vol(int32_t reg) { return static_cast<int16_t>(reg); }
int16_t sat(int32_t v) { return static_cast<int16_t>(std::clamp(v, -32768, 32767)); }

int32_t volume(uint16_t reg) {
    if (reg & 0x8000) return 0x4000;   // sweep mode: not modelled, half volume
    return static_cast<int16_t>(reg << 1);
}

}  // namespace

void decodeBlock(const uint8_t* b, int32_t& s1, int32_t& s2, int16_t out[28]) {
    int shift = b[0] & 15, filter = (b[0] >> 4) & 7;
    if (shift > 12) shift = 9;
    if (filter > 4) filter = 4;
    const int f0 = kFilter[filter][0], f1 = kFilter[filter][1];
    for (int i = 0; i < 28; ++i) {
        const int nibble = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
        int32_t s = static_cast<int16_t>(nibble << 12) >> shift;
        s += (s1 * f0 + s2 * f1 + 32) >> 6;
        s = std::clamp(s, -32768, 32767);
        s2 = s1;
        s1 = s;
        out[i] = static_cast<int16_t>(s);
    }
}

// ---- reverb

void Reverb::set(int newMode, int delay, int feedback) {
    newMode = std::clamp(newMode, 0, 9);
    if (newMode == mode) return;
    mode = newMode;
    for (int i = 0; i < 32; ++i) r[i] = kReverbPresets[mode][i];
    if (mode == 7) r[vWALL] = static_cast<uint16_t>(-(feedback & 0x7F) * 0x100);   // echo feedback
    (void)delay;   // GT4 sends 0; the echo and delay presets keep their full length
    buf.assign(std::max<uint32_t>(kReverbBytes[mode] / 2, 16), 0);
    pos = 0;
}

int16_t& Reverb::at(int32_t reg, int32_t back) {
    const int32_t n = static_cast<int32_t>(buf.size());
    int32_t i = (static_cast<int32_t>(pos) + reg * 4 - back) % n;
    return buf[i < 0 ? i + n : i];
}

// One 24 kHz step: the wet input in, the reverberated pair out.
void Reverb::step(int32_t inl, int32_t inr) {
    const int32_t lin = vol(r[vLIN]) * std::clamp(inl, -32768, 32767) >> 15;
    const int32_t rin = vol(r[vRIN]) * std::clamp(inr, -32768, 32767) >> 15;
    const int32_t iir = vol(r[vIIR]), wall = vol(r[vWALL]);
    auto reflect = [&](int32_t dst, int32_t src, int32_t input) {
        const int32_t old = at(r[dst], 1);
        const int32_t v = ((input + (at(r[src]) * wall >> 15) - old) * iir >> 15) + old;
        at(r[dst]) = sat(v);
    };
    reflect(mLSAME, dLSAME, lin);
    reflect(mRSAME, dRSAME, rin);
    reflect(mLDIFF, dRDIFF, lin);
    reflect(mRDIFF, dLDIFF, rin);
    int32_t l = (vol(r[vCOMB1]) * at(r[mLCOMB1]) + vol(r[vCOMB2]) * at(r[mLCOMB2]) + vol(r[vCOMB3]) * at(r[mLCOMB3]) +
                 vol(r[vCOMB4]) * at(r[mLCOMB4])) >> 15;
    int32_t rr = (vol(r[vCOMB1]) * at(r[mRCOMB1]) + vol(r[vCOMB2]) * at(r[mRCOMB2]) + vol(r[vCOMB3]) * at(r[mRCOMB3]) +
                  vol(r[vCOMB4]) * at(r[mRCOMB4])) >> 15;
    auto allpass = [&](int32_t m, int32_t d, int32_t v, int32_t x) {
        const int32_t delayed = at(r[m] - r[d]);
        x -= vol(r[v]) * delayed >> 15;
        at(r[m]) = sat(x);
        return (x * vol(r[v]) >> 15) + delayed;
    };
    l = allpass(mLAPF1, dAPF1, vAPF1, l);
    rr = allpass(mRAPF1, dAPF1, vAPF1, rr);
    l = allpass(mLAPF2, dAPF2, vAPF2, l);
    rr = allpass(mRAPF2, dAPF2, vAPF2, rr);
    prev[0] = out[0];
    prev[1] = out[1];
    out[0] = l * depth[0] >> 15;
    out[1] = rr * depth[1] >> 15;
    pos = (pos + 1) % static_cast<uint32_t>(buf.size());
}

// The input accumulates over two samples, the output is interpolated.
void Reverb::sample(int32_t inl, int32_t inr, int32_t& outl, int32_t& outr) {
    if (!on || mode <= 0) return;
    if (!odd) {
        in[0] = inl;
        in[1] = inr;
        outl += (prev[0] + out[0]) / 2;
        outr += (prev[1] + out[1]) / 2;
    } else {
        step((in[0] + inl) / 2, (in[1] + inr) / 2);
        outl += prev[0];
        outr += prev[1];
    }
    odd = !odd;
}

// ---- voices

// One step of the envelope generator (psx-spx "SPU ADSR"): every AdsrCycles samples the level moves by AdsrStep;
// exponential increases slow down above 0x6000, exponential decreases are proportional to the level.
void envelope(Voice& v) {
    bool exp = false, dec = false;
    int shift = 0, step = 0;
    switch (v.phase) {
    case kAttack:
        exp = v.adsr1 >> 15;
        shift = (v.adsr1 >> 10) & 31;
        step = 7 - ((v.adsr1 >> 8) & 3);
        break;
    case kDecay:
        exp = dec = true;
        shift = (v.adsr1 >> 4) & 15;
        step = -8;
        break;
    case kSustain:
        exp = v.adsr2 >> 15;
        dec = (v.adsr2 >> 14) & 1;
        shift = (v.adsr2 >> 8) & 31;
        step = dec ? -8 + ((v.adsr2 >> 6) & 3) : 7 - ((v.adsr2 >> 6) & 3);
        break;
    case kRelease:
        exp = (v.adsr2 >> 5) & 1;
        dec = true;
        shift = v.adsr2 & 31;
        step = -8;
        break;
    default:
        return;
    }
    if (v.env_wait > 0 && --v.env_wait > 0) return;
    int cycles = 1 << std::max(0, shift - 11);
    int32_t delta = step << std::max(0, 11 - shift);
    if (exp && !dec && v.env > 0x6000) cycles *= 4;
    if (exp && dec) delta = (delta * v.env) >> 15;
    v.env_wait = cycles;
    v.env = std::clamp(v.env + delta, 0, 0x7FFF);
    // a new phase starts counting at once (a long sustain wait must not delay the release)
    if (v.phase == kAttack && v.env >= 0x7FFF) {
        v.phase = kDecay;
        v.env_wait = 0;
    } else if (v.phase == kDecay && v.env <= ((v.adsr1 & 15) + 1) * 0x800) {
        v.phase = kSustain;
        v.env_wait = 0;
    } else if (v.phase == kRelease && v.env == 0) {
        v.phase = kOff;
    }
}

// The block at v.block: its loop flags and its samples.
void Spu2::loadBlock(Voice& v) {
    v.block &= kRamBytes - 16;
    const uint8_t* b = ram_ + v.block;
    v.flags = b[1];
    if (v.flags & 4) v.loop = v.block;
    decodeBlock(b, v.s1, v.s2, v.samples);
}

void Spu2::keyOn(Voice& v) {
    v.phase = kAttack;
    v.env = 0;
    v.env_wait = 0;
    v.block = v.loop = v.ssa & ~15u;
    v.s1 = v.s2 = 0;
    v.index = 0;
    v.counter = 0;
    v.last = v.cur = 0;
    loadBlock(v);
    v.cur = v.samples[0];
}

// One output sample of a voice (before volumes; not computed when not mixing), advancing it by its pitch.
template <bool kMix> int32_t Spu2::voiceSample(Core& c, int n, Voice& v) {
    int32_t s = 0;
    if constexpr (kMix) {
        const int32_t frac = v.counter & 0xFFF;
        s = v.last + (((v.cur - v.last) * frac) >> 12);
    }
    v.counter += std::min<uint32_t>(v.pitch, 0x3FFF);
    while (v.counter >= 0x1000) {
        v.counter -= 0x1000;
        if (++v.index == 28) {
            v.index = 0;
            if (v.flags & 1) {   // loop end: jump to the loop start, or stop
                c.endx |= 1u << n;
                if (v.flags & 2) {
                    v.block = v.loop;
                } else {
                    v.phase = kOff;
                    v.env = 0;
                    return 0;
                }
            } else {
                v.block += 16;
            }
            loadBlock(v);
        }
        v.last = v.cur;
        v.cur = v.samples[v.index];
    }
    return s;
}

template <bool kMix> void Spu2::run(int32_t* mix, int n, bool dryOnly) {
    std::vector<int32_t> wet(kMix ? 2 * n : 0);
    for (int c = 0; c < 2; ++c) {
        Core& cr = core[c];
        std::fill(wet.begin(), wet.end(), 0);
        const bool fx = cr.fx.on && cr.fx.mode > 0 && !dryOnly;
        for (int k = 0; k < 24; ++k) {
            Voice& v = cr.v[k];
            if (v.phase == kOff) continue;
            int32_t gl = 0, gr = 0, el = 0, er = 0;
            if constexpr (kMix) {
                const bool dry_l = cr.vmixl >> k & 1, dry_r = cr.vmixr >> k & 1;
                const bool wet_l = cr.vmixel >> k & 1, wet_r = cr.vmixer >> k & 1;
                const int32_t vl = volume(v.voll), vr = volume(v.volr);
                // without the reverb, effect-only voices come back dry at the effect return volume
                gl = dry_l ? vl : (!fx && (wet_l || wet_r)) ? vl * std::abs(cr.evol[0]) >> 15 : 0;
                gr = dry_r ? vr : (!fx && (wet_l || wet_r)) ? vr * std::abs(cr.evol[1]) >> 15 : 0;
                el = fx && wet_l ? vl : 0;
                er = fx && wet_r ? vr : 0;
            }
            for (int i = 0; i < n && v.phase != kOff; ++i) {
                const int32_t s = voiceSample<kMix>(cr, k, v) * v.env >> 15;
                envelope(v);
                if constexpr (kMix) {
                    mix[2 * i] += s * gl >> 15;
                    mix[2 * i + 1] += s * gr >> 15;
                    wet[2 * i] += s * el >> 15;
                    wet[2 * i + 1] += s * er >> 15;
                }
            }
        }
        if constexpr (kMix)
            if (fx)
                for (int i = 0; i < n; ++i) cr.fx.sample(wet[2 * i], wet[2 * i + 1], mix[2 * i], mix[2 * i + 1]);
    }
}

void Spu2::advance(int n) { run<false>(nullptr, n, false); }
void Spu2::mix(int32_t* stereo, int n, bool dryOnly) { run<true>(stereo, n, dryOnly); }

// ---- the register image and the status

void Spu2::apply(const uint8_t* image) {
    for (int c = 0; c < 2; ++c) {
        const uint8_t* p = image + c * 0x1D0;
        Core& cr = core[c];
        const uint32_t flags = rd32(p), mask = rd32(p + 4);
        for (int n = 0; n < 24; ++n) {
            if (!(mask >> n & 1)) continue;
            const uint8_t* r = p + 8 + 16 * n;
            const uint16_t f = rd16(r);
            Voice& v = cr.v[n];
            if (f & 0x01) v.pitch = rd16(r + 2);
            if (f & 0x02) v.voll = rd16(r + 4);
            if (f & 0x04) v.volr = rd16(r + 6);
            if (f & 0x08) v.ssa = rd32(r + 8);
            if (f & 0x10) v.adsr1 = rd16(r + 12);
            if (f & 0x20) v.adsr2 = rd16(r + 14);
        }
        {   // effect (sceSdSetEffectAttr): +0 set, +1 enable, +2 clear the work area, +3 mode, +4/+6 depth L/R,
            // +8 delay, +0xA feedback, +0xC end of the work area
            const uint8_t* e = p + 0x1C0;
            if (std::memcmp(cr.effect, e, 0x10) != 0) {   // (applied when they change; all zeros at the start)
                std::memcpy(cr.effect, e, 0x10);
                Reverb& fx = cr.fx;
                fx.on = e[1] != 0;
                fx.set(e[3], rd16(e + 8), rd16(e + 10));
                fx.depth[0] = static_cast<int16_t>(rd16(e + 4));
                fx.depth[1] = static_cast<int16_t>(rd16(e + 6));
                if (e[2]) std::fill(fx.buf.begin(), fx.buf.end(), int16_t{0});
                if (e[0] && onEffect) onEffect(c, fx, e);
            }
        }
        if (flags & 0x2) {
            cr.evol[0] = static_cast<int16_t>(rd16(p + 0x18C));
            cr.evol[1] = static_cast<int16_t>(rd16(p + 0x18E));
        }
        if (flags & 0x40) cr.vmixl = rd32(p + 0x1A0);
        if (flags & 0x80) cr.vmixel = rd32(p + 0x1A4);
        if (flags & 0x100) cr.vmixr = rd32(p + 0x1A8);
        if (flags & 0x200) cr.vmixer = rd32(p + 0x1AC);
        if (flags & 0x1000) {
            const uint32_t off = rd32(p + 0x1B8);
            for (int n = 0; n < 24; ++n)
                if (off >> n & 1) {
                    ++counters.koff;
                    if (cr.v[n].phase != kOff) {
                        cr.v[n].phase = kRelease;
                        cr.v[n].env_wait = 0;
                    }
                }
        }
        if (flags & 0x800) {
            const uint32_t on = rd32(p + 0x1B4);
            for (int n = 0; n < 24; ++n)
                if (on >> n & 1) {
                    Counters& k = counters;
                    ++k.kon;
                    ++k.konTotal;
                    if (cr.v[n].phase == kRelease) ++k.konReleasing;
                    else if (cr.v[n].phase != kOff) ++k.konKeyed;
                    const uint32_t gap = k.frame - k.konFrame[c][n];
                    if (k.konFrame[c][n] && gap >= 1 && gap <= 4) ++k.konAgain[gap];
                    k.konFrame[c][n] = k.frame;
                    keyOn(cr.v[n]);
                    cr.endx &= ~(1u << n);
                }
        }
    }
}

// ---- the state, for the other side (saveState / loadState)

namespace {

static_assert(std::is_trivially_copyable_v<Voice>);
constexpr uint32_t kStateMagic = 0x32555053;   // "SPU2"
constexpr uint32_t kStateVersion = 1;

struct Writer {
    std::vector<uint8_t>& out;
    template <typename T> void put(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), p, p + sizeof v);
    }
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    }
};

struct Reader {
    const uint8_t* at;
    const uint8_t* end;
    bool ok = true;
    template <typename T> void get(T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes(&v, sizeof v);
    }
    void bytes(void* p, size_t n) {
        if (!ok || static_cast<size_t>(end - at) < n) {
            ok = false;
            return;
        }
        std::memcpy(p, at, n);
        at += n;
    }
};

}  // namespace

std::vector<uint8_t> Spu2::saveState() const {
    std::vector<uint8_t> out;
    Writer w{out};
    w.put(kStateMagic);
    w.put(kStateVersion);
    for (const Core& c : core) {
        w.bytes(c.v, sizeof c.v);
        w.put(c.endx);
        w.put(c.vmixl);
        w.put(c.vmixr);
        w.put(c.vmixel);
        w.put(c.vmixer);
        w.bytes(c.evol, sizeof c.evol);
        w.bytes(c.effect, sizeof c.effect);
        const Reverb& f = c.fx;
        w.put(f.on);
        w.put(f.mode);
        w.bytes(f.depth, sizeof f.depth);
        w.bytes(f.r, sizeof f.r);
        w.put(static_cast<uint32_t>(f.buf.size()));
        w.bytes(f.buf.data(), f.buf.size() * sizeof f.buf[0]);
        w.put(f.pos);
        w.bytes(f.in, sizeof f.in);
        w.bytes(f.out, sizeof f.out);
        w.bytes(f.prev, sizeof f.prev);
        w.put(f.odd);
    }
    return out;
}

bool Spu2::loadState(const uint8_t* data, size_t size) {
    Reader r{data, data + size};
    uint32_t magic = 0, version = 0;
    r.get(magic);
    r.get(version);
    if (!r.ok || magic != kStateMagic || version != kStateVersion) return false;
    Core loaded[2];
    for (Core& c : loaded) {
        r.bytes(c.v, sizeof c.v);
        // (a state can come from a file, a replayed capture: values that would index past a table, or spin the pitch
        // loop, refuse it. A voice that ended inside the pitch loop keeps up to 0x3FFE in its counter: below the bound)
        for (const Voice& v : c.v) {
            const int phase = static_cast<int>(v.phase);
            if (r.ok && (phase < kOff || phase > kRelease || v.index < 0 || v.index >= 28 || v.counter >= 0x8000 ||
                         v.env < 0 || v.env > 0x7FFF))
                return false;
        }
        r.get(c.endx);
        r.get(c.vmixl);
        r.get(c.vmixr);
        r.get(c.vmixel);
        r.get(c.vmixer);
        r.bytes(c.evol, sizeof c.evol);
        r.bytes(c.effect, sizeof c.effect);
        Reverb& f = c.fx;
        r.get(f.on);
        r.get(f.mode);
        r.bytes(f.depth, sizeof f.depth);
        r.bytes(f.r, sizeof f.r);
        uint32_t n = 0;
        r.get(n);
        if (!r.ok || n > (1u << 20)) return false;
        f.buf.assign(n, 0);
        r.bytes(f.buf.data(), n * sizeof f.buf[0]);
        r.get(f.pos);
        r.bytes(f.in, sizeof f.in);
        r.bytes(f.out, sizeof f.out);
        r.bytes(f.prev, sizeof f.prev);
        r.get(f.odd);
        if (r.ok && (f.buf.empty() || f.pos >= f.buf.size())) return false;
    }
    if (!r.ok || r.at != r.end) return false;
    core[0] = std::move(loaded[0]);
    core[1] = std::move(loaded[1]);
    return true;
}

void Spu2::status(uint8_t* reply) const {
    for (int c = 0; c < 2; ++c) {
        uint8_t* r = reply + c * 0x34;
        uint32_t endx = core[c].endx;
        for (int n = 0; n < 24; ++n)
            if (core[c].v[n].phase == kOff) endx |= 1u << n;
        std::memcpy(r, &endx, 4);
        for (int n = 0; n < 24; ++n) {
            const uint16_t env = static_cast<uint16_t>(core[c].v[n].phase == kOff ? 0 : core[c].v[n].env);
            std::memcpy(r + 4 + 2 * n, &env, 2);
        }
    }
}

}  // namespace gt4::spu
