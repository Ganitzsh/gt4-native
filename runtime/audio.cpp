// The game's sound (audio.h). The SPU2 part follows the hardware as the PS1/PS2 documentation
// describes it (SPU ADPCM blocks, the ADSR envelope generator, 0x1000 = 48 kHz pitch, the reverb
// unit); GT4_REVERB=off mixes the effect sends in dry at the effect return volume instead.
#include "audio.h"

#include "gt4/link/protocol.h"
#include "link.h"
#include "log.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace gt4::rt {
namespace {

constexpr int kRate = 48000;
std::mutex g_mutex;
std::vector<uint8_t> g_ram(2 << 20);

uint16_t rd16(const uint8_t *p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// ---- SPU ADPCM: 16-byte blocks of 28 samples (shift/filter, flags, 14 bytes of nibbles)

const int kFilter[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

void decode_block(const uint8_t *b, int32_t &s1, int32_t &s2, int16_t out[28]) {
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

// ---- SPU2 voices

enum Phase { kOff, kAttack, kDecay, kSustain, kRelease };

struct Voice {
    uint16_t pitch = 0, voll = 0, volr = 0, adsr1 = 0, adsr2 = 0;
    uint32_t ssa = 0;
    Phase phase = kOff;
    int32_t env = 0, env_wait = 0;
    uint32_t block = 0, loop = 0;  // byte addresses in SPU RAM
    uint8_t flags = 0;             // the current block's loop flags
    int16_t samples[28] = {};
    int32_t s1 = 0, s2 = 0;
    int index = 0;           // sample in the block
    uint32_t counter = 0;    // 12-bit fraction
    int16_t last = 0, cur = 0;
};

// ---- SPU2 reverb (psx-spx "SPU Reverb Formula"): the same unit as the PS1's, running at half the output rate
// on a work area of its own. The game picks one of libsd's presets (sceSdSetEffectAttr: mode, depth, delay,
// feedback); the register values are the documented ones (psx-spx "Reverb Examples"), in register order
// dAPF1 .. vRIN. Offsets are in 8-byte units (4 samples).

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

struct Reverb {
    bool on = false;
    int mode = -1;
    int32_t depth[2] = {0, 0};
    int32_t r[32] = {};
    std::vector<int16_t> buf = std::vector<int16_t>(16);
    uint32_t pos = 0;
    int32_t in[2] = {0, 0};      // the wet input of the first half of the current 24 kHz step
    int32_t out[2] = {0, 0}, prev[2] = {0, 0};
    bool odd = false;

    void set(int new_mode, int delay, int feedback) {
        new_mode = std::clamp(new_mode, 0, 9);
        if (new_mode == mode) return;
        mode = new_mode;
        for (int i = 0; i < 32; ++i) r[i] = kReverbPresets[mode][i];
        if (mode == 7) r[vWALL] = static_cast<uint16_t>(-(feedback & 0x7F) * 0x100);   // echo feedback
        (void)delay;   // GT4 sends 0; the echo and delay presets keep their full length
        buf.assign(std::max<uint32_t>(kReverbBytes[mode] / 2, 16), 0);
        pos = 0;
    }
    static int32_t vol(int32_t reg) { return static_cast<int16_t>(reg); }
    int16_t &at(int32_t reg, int32_t back = 0) {
        const int32_t n = static_cast<int32_t>(buf.size());
        int32_t i = (static_cast<int32_t>(pos) + reg * 4 - back) % n;
        return buf[i < 0 ? i + n : i];
    }
    static int16_t sat(int32_t v) { return static_cast<int16_t>(std::clamp(v, -32768, 32767)); }
    // One 24 kHz step: the wet input in, the reverberated pair out.
    void step(int32_t inl, int32_t inr) {
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
    // One 48 kHz output sample: the input accumulates over two samples, the output is interpolated.
    void sample(int32_t inl, int32_t inr, int32_t &outl, int32_t &outr) {
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
};

struct Core {
    Voice v[24];
    uint32_t endx = 0;
    uint32_t vmixl = 0xFFFFFF, vmixr = 0xFFFFFF, vmixel = 0, vmixer = 0;
    int16_t evol[2] = {0, 0};
    Reverb fx;
};
Core g_core[2];
uint32_t g_kon_count = 0, g_koff_count = 0;  // for GT4_TRACE_SPU

void load_block(Voice &v) {
    v.block &= (2 << 20) - 16;
    const uint8_t *b = &g_ram[v.block];
    v.flags = b[1];
    if (v.flags & 4) v.loop = v.block;
    decode_block(b, v.s1, v.s2, v.samples);
}

void key_on(Voice &v) {
    v.phase = kAttack;
    v.env = 0;
    v.env_wait = 0;
    v.block = v.loop = v.ssa & ~15u;
    v.s1 = v.s2 = 0;
    v.index = 0;
    v.counter = 0;
    v.last = v.cur = 0;
    load_block(v);
    v.cur = v.samples[0];
}

// One step of the envelope generator (psx-spx "SPU ADSR"): every AdsrCycles samples the level
// moves by AdsrStep; exponential increases slow down above 0x6000, exponential decreases are
// proportional to the level.
void envelope(Voice &v) {
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

int32_t volume(uint16_t reg) {
    if (reg & 0x8000) return 0x4000;  // sweep mode: not modelled, half volume
    return static_cast<int16_t>(reg << 1);
}

// One output sample of a voice (before volumes), advancing it by its pitch.
int32_t voice_sample(Core &core, int n, Voice &v) {
    const int32_t frac = v.counter & 0xFFF;
    const int32_t s = v.last + (((v.cur - v.last) * frac) >> 12);
    v.counter += std::min<uint32_t>(v.pitch, 0x3FFF);
    while (v.counter >= 0x1000) {
        v.counter -= 0x1000;
        if (++v.index == 28) {
            v.index = 0;
            if (v.flags & 1) {  // loop end: jump to the loop start, or stop
                core.endx |= 1u << n;
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
            load_block(v);
        }
        v.last = v.cur;
        v.cur = v.samples[v.index];
    }
    return s;
}

// ---- BGM: .ads ("SShd" header, then "SSbd" data) streamed from the disc image

struct Bgm {
    bool playing = false, paused = false, loop = false, finished = false, mono = false, started = false;
    uint64_t start = 0, data = 0, pos = 0, end = 0;  // byte offsets in the image
    uint32_t codec = 0, rate = kRate, channels = 2, interleave = 0x200;
    std::deque<int16_t> fifo;  // decoded stereo frames at the stream's rate
    int32_t adpcm_hist[2][2] = {};
    uint64_t frac = 0;         // resampling position (16.16, input frames)
    int16_t prev[2] = {}, next[2] = {};
    int64_t samples = 0;
    uint32_t loops = 0;
    int32_t volume = 0x8000, fade = 0x7FFF, fade_step = 0, fade_tick = 0;
} g_bgm;

void bgm_refill() {
    Bgm &b = g_bgm;
    const uint32_t chunk = b.interleave * b.channels;
    std::vector<uint8_t> buf(chunk);
    while (b.fifo.size() < 4096 && b.playing) {
        if (b.pos + chunk > b.end) {
            if (!b.loop) {
                b.playing = false;
                b.finished = true;
                return;
            }
            b.pos = b.data;
            ++b.loops;
            std::memset(b.adpcm_hist, 0, sizeof b.adpcm_hist);
        }
        disc_read(b.pos, buf.data(), chunk);
        b.pos += chunk;
        if (b.codec == 1) {  // 16-bit PCM, `interleave` bytes per channel
            const uint32_t n = b.interleave / 2;
            for (uint32_t i = 0; i < n; ++i)
                for (uint32_t c = 0; c < 2; ++c) {
                    const uint32_t ch = std::min(c, b.channels - 1);
                    b.fifo.push_back(static_cast<int16_t>(rd16(&buf[ch * b.interleave + i * 2])));
                }
        } else {  // SPU ADPCM, `interleave` bytes per channel
            const uint32_t blocks = b.interleave / 16;
            std::vector<int16_t> pcm[2];
            for (uint32_t c = 0; c < b.channels && c < 2; ++c) {
                pcm[c].resize(blocks * 28);
                for (uint32_t k = 0; k < blocks; ++k)
                    decode_block(&buf[c * b.interleave + k * 16], b.adpcm_hist[c][0], b.adpcm_hist[c][1],
                                 &pcm[c][k * 28]);
            }
            for (uint32_t i = 0; i < blocks * 28; ++i) {
                b.fifo.push_back(pcm[0][i]);
                b.fifo.push_back(pcm[b.channels > 1 ? 1 : 0][i]);
            }
        }
    }
}

void bgm_mix(int32_t *out, int n) {
    Bgm &b = g_bgm;
    if (!b.playing || b.paused) return;
    for (int i = 0; i < n; ++i) {
        if ((++b.fade_tick % (kRate / 100)) == 0 && b.fade_step) {  // 10 ms fade steps
            b.fade = std::clamp(b.fade + b.fade_step, 0, 0x7FFF);
            if (b.fade == 0 || b.fade == 0x7FFF) b.fade_step = 0;
            if (b.fade == 0) {
                b.playing = false;
                b.finished = true;
                return;
            }
        }
        b.frac += (static_cast<uint64_t>(b.rate) << 16) / kRate;
        while (b.frac >= 0x10000) {
            b.frac -= 0x10000;
            if (b.fifo.size() < 2) bgm_refill();
            if (b.fifo.size() < 2) return;
            b.prev[0] = b.next[0];
            b.prev[1] = b.next[1];
            b.next[0] = b.fifo.front();
            b.fifo.pop_front();
            b.next[1] = b.fifo.front();
            b.fifo.pop_front();
            ++b.samples;
        }
        const int32_t f = static_cast<int32_t>(b.frac);
        int32_t l = b.prev[0] + (((b.next[0] - b.prev[0]) * f) >> 16);
        int32_t r = b.prev[1] + (((b.next[1] - b.prev[1]) * f) >> 16);
        if (b.mono) l = r = (l + r) / 2;
        const int64_t gain = static_cast<int64_t>(b.volume) * b.fade >> 15;  // 0..0x8000
        out[2 * i] += static_cast<int32_t>(l * gain >> 15);
        out[2 * i + 1] += static_cast<int32_t>(r * gain >> 15);
    }
}

// ---- movie audio

std::deque<int16_t> g_movie;

// ---- output

FILE *g_wav = nullptr;
uint32_t g_wav_bytes = 0;
uint64_t g_frames_due = 0;  // in 1/60000 of a sample frame

void wav_header() {
    struct {
        char riff[4];
        uint32_t size;
        char wave[4], fmt[4];
        uint32_t fmt_size;
        uint16_t format, channels;
        uint32_t rate, bytes_per_sec;
        uint16_t align, bits;
        char data[4];
        uint32_t data_size;
    } h{{'R', 'I', 'F', 'F'}, 36 + g_wav_bytes, {'W', 'A', 'V', 'E'}, {'f', 'm', 't', ' '}, 16, 1, 2,
        kRate, kRate * 4, 4, 16, {'d', 'a', 't', 'a'}, g_wav_bytes};
    std::fseek(g_wav, 0, SEEK_SET);
    std::fwrite(&h, sizeof h, 1, g_wav);
    std::fseek(g_wav, 0, SEEK_END);
}

}  // namespace

void audio_init() {
    // GT4_REVERB_TEST=MODE: the reverb's response to one full-scale sample, in 100 ms steps (a check of the unit).
    if (const char *t = std::getenv("GT4_REVERB_TEST"); t && *t) {
        Reverb fx;
        fx.on = true;
        fx.set(std::atoi(t), 0, 0);
        fx.depth[0] = fx.depth[1] = 0x7FFF;
        char line[512];
        int len = std::snprintf(line, sizeof line, "reverb mode %d impulse response (rms per 100 ms):", fx.mode);
        double sum = 0;
        for (int i = 0; i < kRate * 2; ++i) {
            int32_t l = 0, r = 0;
            fx.sample(i < 2 ? 0x7FFF : 0, i < 2 ? 0x7FFF : 0, l, r);
            sum += double(l) * l + double(r) * r;
            if ((i + 1) % (kRate / 10) == 0 && len < 480) {
                len += std::snprintf(line + len, sizeof line - len, " %.0f", std::sqrt(sum / (kRate / 5)));
                sum = 0;
            }
        }
        log_info("%s", line);
    }
    if (const char *path = std::getenv("GT4_AUDIO_WAV"); path && *path) {
        g_wav = std::fopen(path, "wb");
        if (g_wav) {
            wav_header();
            log_info("audio: writing the mix to %s", path);
        }
    }
}

uint8_t *spu_ram() { return g_ram.data(); }

void spu_apply(const uint8_t *image) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int c = 0; c < 2; ++c) {
        const uint8_t *p = image + c * 0x1D0;
        Core &core = g_core[c];
        const uint32_t flags = rd32(p), mask = rd32(p + 4);
        for (int n = 0; n < 24; ++n) {
            if (!(mask >> n & 1)) continue;
            const uint8_t *r = p + 8 + 16 * n;
            const uint16_t f = rd16(r);
            Voice &v = core.v[n];
            if (f & 0x01) v.pitch = rd16(r + 2);
            if (f & 0x02) v.voll = rd16(r + 4);
            if (f & 0x04) v.volr = rd16(r + 6);
            if (f & 0x08) v.ssa = rd32(r + 8);
            if (f & 0x10) v.adsr1 = rd16(r + 12);
            if (f & 0x20) v.adsr2 = rd16(r + 14);
        }
        {   // effect (sceSdSetEffectAttr): +0 set, +1 enable, +2 clear the work area, +3 mode, +4/+6 depth L/R,
            // +8 delay, +0xA feedback, +0xC end of the work area
            static uint8_t last[2][0x10];
            const uint8_t *e = p + 0x1C0;
            if (std::memcmp(last[c], e, 0x10) != 0) {
                std::memcpy(last[c], e, 0x10);
                Reverb &fx = core.fx;
                fx.on = e[1] != 0;
                fx.set(e[3], rd16(e + 8), rd16(e + 10));
                fx.depth[0] = static_cast<int16_t>(rd16(e + 4));
                fx.depth[1] = static_cast<int16_t>(rd16(e + 6));
                if (e[2]) std::fill(fx.buf.begin(), fx.buf.end(), 0);
                if (e[0])
                    log_info("spu: core %d reverb %s, mode %d, depth %d/%d, delay %d, feedback %d", c, fx.on ? "on" : "off",
                             e[3], fx.depth[0], fx.depth[1], rd16(e + 8), rd16(e + 10));
            }
        }
        if (flags & 0x2) {
            core.evol[0] = static_cast<int16_t>(rd16(p + 0x18C));
            core.evol[1] = static_cast<int16_t>(rd16(p + 0x18E));
        }
        if (flags & 0x40) core.vmixl = rd32(p + 0x1A0);
        if (flags & 0x80) core.vmixel = rd32(p + 0x1A4);
        if (flags & 0x100) core.vmixr = rd32(p + 0x1A8);
        if (flags & 0x200) core.vmixer = rd32(p + 0x1AC);
        if (flags & 0x1000) {
            const uint32_t off = rd32(p + 0x1B8);
            for (int n = 0; n < 24; ++n)
                if (off >> n & 1) {
                    ++g_koff_count;
                    if (core.v[n].phase != kOff) {
                        core.v[n].phase = kRelease;
                        core.v[n].env_wait = 0;
                    }
                }
        }
        if (flags & 0x800) {
            const uint32_t on = rd32(p + 0x1B4);
            for (int n = 0; n < 24; ++n)
                if (on >> n & 1) {
                    ++g_kon_count;
                    key_on(core.v[n]);
                    core.endx &= ~(1u << n);
                }
        }
    }
}

void spu_status(uint8_t *reply) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int c = 0; c < 2; ++c) {
        uint8_t *r = reply + c * 0x34;
        uint32_t endx = g_core[c].endx;
        for (int n = 0; n < 24; ++n)
            if (g_core[c].v[n].phase == kOff) endx |= 1u << n;
        std::memcpy(r, &endx, 4);
        for (int n = 0; n < 24; ++n) {
            const uint16_t env = static_cast<uint16_t>(g_core[c].v[n].phase == kOff ? 0 : g_core[c].v[n].env);
            std::memcpy(r + 4 + 2 * n, &env, 2);
        }
    }
}

void bgm_play(uint32_t lba, uint32_t size, bool loop) {
    uint8_t h[0x28];
    const uint64_t start = static_cast<uint64_t>(lba) * 2048;
    if (!disc_read(start, h, sizeof h) || std::memcmp(h, "SShd", 4) != 0) {
        log_warn("bgm: no SShd header at LBA %u", lba);
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    Bgm &b = g_bgm;
    const int32_t volume = b.volume;
    const bool mono = b.mono;
    b = Bgm{};
    b.volume = volume;
    b.mono = mono;
    b.codec = rd32(h + 8);
    b.rate = rd32(h + 12) ? rd32(h + 12) : kRate;
    b.channels = std::clamp<uint32_t>(rd32(h + 16), 1, 2);
    b.interleave = rd32(h + 20) ? rd32(h + 20) : 0x200;
    const uint32_t bytes = rd32(h + 0x24);
    b.start = start;
    b.data = b.pos = start + 0x28;
    b.end = b.data + std::min<uint64_t>(bytes, size > 0x28 ? size - 0x28 : bytes);
    b.loop = loop;
    b.playing = true;
    b.started = true;
    log_info("bgm: LBA %u, codec %u, %u Hz, %u channel(s), interleave %#x, %u bytes%s", lba, b.codec, b.rate,
             b.channels, b.interleave, bytes, loop ? ", looping" : "");
}

void bgm_stop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bgm.playing = false;
    g_bgm.fifo.clear();
}

void bgm_fade(int32_t target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bgm.fade_step = target;  // signed amount per 10 ms
    if (target > 0 && !g_bgm.playing) g_bgm.fade = 0;
}

void bgm_volume(int32_t volume) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bgm.volume = std::min(std::abs(volume), 0x8000);
}

void bgm_pause(bool paused) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bgm.paused = paused;
}

void bgm_mono(bool mono) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bgm.mono = mono;
}

void bgm_status(uint8_t *reply) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::memset(reply, 0, 0x40);
    if (!g_bgm.started) return;  // nothing played yet: the IOP's status block is still zero
    const int64_t us = g_bgm.rate ? g_bgm.samples * 1000000 / g_bgm.rate : 0;
    const uint32_t finished = g_bgm.finished || !g_bgm.playing;
    std::memcpy(reply, &us, 8);
    std::memcpy(reply + 8, &g_bgm.samples, 8);
    std::memcpy(reply + 0x10, &g_bgm.loops, 4);
    std::memcpy(reply + 0x14, &finished, 4);
}

void movie_audio_push(const int16_t *frames, int count) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_movie.insert(g_movie.end(), frames, frames + 2 * count);
    const size_t cap = 2 * kRate * 2;  // two seconds
    if (g_movie.size() > cap) g_movie.erase(g_movie.begin(), g_movie.begin() + (g_movie.size() - cap));
}

int movie_audio_queued() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_movie.size() / 2);
}

void spu_adpcm_decode(const uint8_t *block, int32_t &s1, int32_t &s2, int16_t out[28]) {
    decode_block(block, s1, s2, out);
}

void movie_audio_clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_movie.clear();
}

// GT4_TRACE_SPU: every 5 s, the voices sounding (core:voice phase env pitch volumes start address)
// and the key-ons and key-offs since the last report.

void spu_trace() {
    static const bool on = std::getenv("GT4_TRACE_SPU") != nullptr;
    static int ticks = 0;
    if (!on || ++ticks % 300) return;
    char line[2048];
    int len = std::snprintf(line, sizeof line, "spu: %u key-ons, %u key-offs;", g_kon_count, g_koff_count);
    g_kon_count = g_koff_count = 0;
    static const char phase[] = "-ADSR";
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 24; ++n) {
            const Voice &v = g_core[c].v[n];
            if (v.phase == kOff || len > static_cast<int>(sizeof line) - 80) continue;
            len += std::snprintf(line + len, sizeof line - len, " %d:%d %c%04x p%04x v%04x/%04x a%05x %04x/%04x", c, n,
                                 phase[v.phase], v.env, v.pitch, v.voll, v.volr, v.ssa, v.adsr1, v.adsr2);
        }
    log_info("%s", line);
}

void audio_frame() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        spu_trace();
    }
    // 48000 / 60 = 800 sample frames a frame (the kernel's frame clock)
    g_frames_due += static_cast<uint64_t>(kRate) * 1000;
    const int n = static_cast<int>(g_frames_due / 60000);
    g_frames_due %= 60000;
    if (n <= 0) return;
    std::vector<int32_t> mix(2 * n, 0);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        static const bool dry_only = std::getenv("GT4_REVERB") && !std::strcmp(std::getenv("GT4_REVERB"), "off");
        std::vector<int32_t> wet(2 * n);
        for (int c = 0; c < 2; ++c) {
            Core &core = g_core[c];
            std::fill(wet.begin(), wet.end(), 0);
            const bool fx = core.fx.on && core.fx.mode > 0 && !dry_only;
            for (int k = 0; k < 24; ++k) {
                Voice &v = core.v[k];
                if (v.phase == kOff) continue;
                const bool dry_l = core.vmixl >> k & 1, dry_r = core.vmixr >> k & 1;
                const bool wet_l = core.vmixel >> k & 1, wet_r = core.vmixer >> k & 1;
                const int32_t vl = volume(v.voll), vr = volume(v.volr);
                // without the reverb, effect-only voices come back dry at the effect return volume
                const int32_t gl = dry_l ? vl : (!fx && (wet_l || wet_r)) ? vl * std::abs(core.evol[0]) >> 15 : 0;
                const int32_t gr = dry_r ? vr : (!fx && (wet_l || wet_r)) ? vr * std::abs(core.evol[1]) >> 15 : 0;
                const int32_t el = fx && wet_l ? vl : 0, er = fx && wet_r ? vr : 0;
                for (int i = 0; i < n && v.phase != kOff; ++i) {
                    const int32_t s = voice_sample(core, k, v) * v.env >> 15;
                    envelope(v);
                    mix[2 * i] += s * gl >> 15;
                    mix[2 * i + 1] += s * gr >> 15;
                    wet[2 * i] += s * el >> 15;
                    wet[2 * i + 1] += s * er >> 15;
                }
            }
            if (fx)
                for (int i = 0; i < n; ++i) core.fx.sample(wet[2 * i], wet[2 * i + 1], mix[2 * i], mix[2 * i + 1]);
        }
        bgm_mix(mix.data(), n);
        const int m = static_cast<int>(std::min<size_t>(g_movie.size() / 2, static_cast<size_t>(n)));
        for (int i = 0; i < 2 * m; ++i) mix[i] += g_movie[i];
        g_movie.erase(g_movie.begin(), g_movie.begin() + 2 * m);
    }
    std::vector<int16_t> out(2 * n);
    for (int i = 0; i < 2 * n; ++i) out[i] = static_cast<int16_t>(std::clamp(mix[i], -32768, 32767));
    if (link_active()) {
        const uint32_t head[2] = {0, kRate};
        link_send(GT4_REC_AUDIO, head, sizeof head, out.data(), static_cast<uint32_t>(out.size() * 2));
    }
    if (g_wav) {
        std::fwrite(out.data(), 2, out.size(), g_wav);
        g_wav_bytes += static_cast<uint32_t>(out.size() * 2);
        static int frames = 0;
        if (++frames % 60 == 0) {
            wav_header();
            std::fflush(g_wav);
        }
    }
}

}  // namespace gt4::rt
