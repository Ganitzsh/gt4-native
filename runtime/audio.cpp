// The game's sound (audio.h). The SPU2 part follows the hardware as the PS1/PS2 documentation
// describes it (SPU ADPCM blocks, the ADSR envelope generator, 0x1000 = 48 kHz pitch); effects
// (reverb) are not rendered yet: voices sent only to the effect bus are mixed in dry at the
// effect return volume.
#include "audio.h"

#include "gt4/link/protocol.h"
#include "link.h"
#include "log.h"
#include "runtime.h"

#include <algorithm>
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

struct Core {
    Voice v[24];
    uint32_t endx = 0;
    uint32_t vmixl = 0xFFFFFF, vmixr = 0xFFFFFF, vmixel = 0, vmixer = 0;
    int16_t evol[2] = {0, 0};
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
uint64_t g_frames_due = 0;  // in 1/1001 frame units

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

void audio_vblank() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        spu_trace();
    }
    // 48000 / 59.94 = 800.8 frames per VBlank
    g_frames_due += static_cast<uint64_t>(kRate) * 1001;
    const int n = static_cast<int>(g_frames_due / 60000);
    g_frames_due %= 60000;
    if (n <= 0) return;
    std::vector<int32_t> mix(2 * n, 0);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int c = 0; c < 2; ++c) {
            Core &core = g_core[c];
            for (int k = 0; k < 24; ++k) {
                Voice &v = core.v[k];
                if (v.phase == kOff) continue;
                const bool dry_l = core.vmixl >> k & 1, dry_r = core.vmixr >> k & 1;
                const bool wet = (core.vmixel | core.vmixer) >> k & 1;
                const int32_t vl = volume(v.voll), vr = volume(v.volr);
                // effect-only voices come back at the effect return volume (no reverb yet)
                const int32_t gl = dry_l ? vl : wet ? vl * std::abs(core.evol[0]) >> 15 : 0;
                const int32_t gr = dry_r ? vr : wet ? vr * std::abs(core.evol[1]) >> 15 : 0;
                for (int i = 0; i < n && v.phase != kOff; ++i) {
                    const int32_t s = voice_sample(core, k, v) * v.env >> 15;
                    envelope(v);
                    mix[2 * i] += s * gl >> 15;
                    mix[2 * i + 1] += s * gr >> 15;
                }
            }
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
