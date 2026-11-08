// The game's sound (audio.h). The SPU2 is platform/audio/spu (one source with the host's mixer, PLAN.md D5);
// GT4_REVERB=off mixes the effect sends in dry at the effect return volume instead.
#include "audio.h"

#include "gt4/link/protocol.h"
#include "link.h"
#include "log.h"
#include "runtime.h"
#include "spu/spu2.h"

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
std::vector<uint8_t> g_ram(spu::kRamBytes);

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

// ---- SPU2: the voices, the reverb, the register image and the status, one source with the host's mixer
// (platform/audio/spu/spu2.h, PLAN.md D5)

spu::Spu2 g_spu{g_ram.data()};

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
                    spu::decodeBlock(&buf[c * b.interleave + k * 16], b.adpcm_hist[c][0], b.adpcm_hist[c][1],
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
        spu::Reverb fx;
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
    g_spu.onEffect = [](int core, const spu::Reverb &fx, const uint8_t *e) {
        log_info("spu: core %d reverb %s, mode %d, depth %d/%d, delay %d, feedback %d", core, fx.on ? "on" : "off",
                 e[3], fx.depth[0], fx.depth[1], rd16(e + 8), rd16(e + 10));
    };
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
    g_spu.apply(image);
}

// RISKS R16: GT4_SPU_REPLY_DELAY=N (0-8, debugging) answers each status with the one N calls before (one a VBlank): how
// late the ENDX / ENVX replies can come, as they would from a mixer on the host, before GT4 runs short of voices or
// cuts sounds. Unset: the status now, as always.
int reply_delay() {
    static const int delay = [] {
        const char *e = std::getenv("GT4_SPU_REPLY_DELAY");
        if (!e || !*e) return -1;
        const int d = std::atoi(e);
        return d < 0 ? 0 : d > 8 ? 8 : d;
    }();
    return delay;
}

void spu_status(uint8_t *reply) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const int delay = reply_delay();
    uint8_t now[0x68];
    uint8_t *out = delay > 0 ? now : reply;
    g_spu.status(out);
    if (delay > 0) {   // a ring of the last statuses: the one `delay` calls back (the oldest kept until it fills)
        static uint8_t ring[9][0x68];
        static int head = 0, filled = 0;
        std::memcpy(ring[head], now, sizeof now);
        const int back = filled < delay ? filled : delay;
        std::memcpy(reply, ring[(head - back + 9) % 9], sizeof now);
        head = (head + 1) % 9;
        if (filled < 8) ++filled;
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
    spu::decodeBlock(block, s1, s2, out);
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
    int len = std::snprintf(line, sizeof line, "spu: %u key-ons, %u key-offs;", g_spu.counters.kon,
                            g_spu.counters.koff);
    g_spu.counters.kon = g_spu.counters.koff = 0;
    static const char phase[] = "-ADSR";
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 24; ++n) {
            const spu::Voice &v = g_spu.core[c].v[n];
            if (v.phase == spu::kOff || len > static_cast<int>(sizeof line) - 80) continue;
            len += std::snprintf(line + len, sizeof line - len, " %d:%d %c%04x p%04x v%04x/%04x a%05x %04x/%04x", c, n,
                                 phase[v.phase], v.env, v.pitch, v.voll, v.volr, v.ssa, v.adsr1, v.adsr2);
        }
    log_info("%s", line);
}

void audio_frame() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        spu_trace();
        // R16's counts, every 600 frames, while GT4_SPU_REPLY_DELAY is set (0 included, for the baseline)
        g_spu.tick();
        const auto &k = g_spu.counters;
        if (reply_delay() >= 0 && k.frame % 600 == 0)
            log_info("spu: R16, reply delay %d: %llu key-ons, %llu onto a voice still keyed, %llu onto one releasing; "
                     "again after 1/2/3/4 frames %llu %llu %llu %llu", reply_delay(),
                     static_cast<unsigned long long>(k.konTotal), static_cast<unsigned long long>(k.konKeyed),
                     static_cast<unsigned long long>(k.konReleasing), static_cast<unsigned long long>(k.konAgain[1]),
                     static_cast<unsigned long long>(k.konAgain[2]), static_cast<unsigned long long>(k.konAgain[3]),
                     static_cast<unsigned long long>(k.konAgain[4]));
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
        g_spu.mix(mix.data(), n, dry_only);
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
