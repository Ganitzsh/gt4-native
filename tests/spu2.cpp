// platform/audio/spu (PLAN.md D5) on SPU RAM and register images built here: ADPCM decoding, a voice keyed on, its
// envelope, a looping and a one-shot sample, key-off and release, the status (ENDX, ENVX), and the property D5 rests
// on: an SPU2 that only advances its voices answers the same status, frame by frame, as one that mixes them.
#include "spu/spu2.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gt4::spu;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

void put16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); }
void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// One register image: per voice its registers (all set), then key-offs and key-ons.
struct Image {
    uint8_t bytes[kImageBytes] = {};
    void voice(int core, int n, uint16_t pitch, uint32_t ssa, uint16_t adsr1, uint16_t adsr2, uint16_t vol = 0x3FFF) {
        uint8_t* p = bytes + core * 0x1D0;
        put32(p + 4, rd32(p + 4) | 1u << n);
        uint8_t* r = p + 8 + 16 * n;
        put16(r, 0x3F);
        put16(r + 2, pitch);
        put16(r + 4, vol);
        put16(r + 6, vol);
        put32(r + 8, ssa);
        put16(r + 12, adsr1);
        put16(r + 14, adsr2);
    }
    void keyOn(int core, int n) {
        uint8_t* p = bytes + core * 0x1D0;
        put32(p, rd32(p) | 0x800);
        put32(p + 0x1B4, rd32(p + 0x1B4) | 1u << n);
    }
    void keyOff(int core, int n) {
        uint8_t* p = bytes + core * 0x1D0;
        put32(p, rd32(p) | 0x1000);
        put32(p + 0x1B8, rd32(p + 0x1B8) | 1u << n);
    }
    void reverb(int core, int mode) {
        uint8_t* e = bytes + core * 0x1D0 + 0x1C0;
        e[0] = 1;
        e[1] = 1;
        e[3] = static_cast<uint8_t>(mode);
        put16(e + 4, 0x3000);
        put16(e + 6, 0x3000);
        uint8_t* p = bytes + core * 0x1D0;
        put32(p, rd32(p) | 0x80 | 0x200);
        put32(p + 0x1A4, 0xFFFFFF);   // every voice to the effect, both sides
        put32(p + 0x1AC, 0xFFFFFF);
    }
    static uint32_t rd32(const uint8_t* p) {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }
};

// `blocks` ADPCM blocks of a saw-ish wave at `at`; the last carries `endFlags` (1 end, 1|2 loop back), the first 4
// (the loop start) when it loops.
void sample(std::vector<uint8_t>& ram, uint32_t at, int blocks, uint8_t endFlags) {
    for (int b = 0; b < blocks; ++b) {
        uint8_t* p = ram.data() + at + 16 * b;
        p[0] = 0x14;   // filter 1, shift 4
        p[1] = static_cast<uint8_t>((b == 0 && (endFlags & 2) ? 4 : 0) | (b == blocks - 1 ? endFlags : 0));
        for (int i = 2; i < 16; ++i) p[i] = static_cast<uint8_t>(0x10 * (i & 7) + (i & 3));
    }
}

uint32_t endx(const uint8_t* status, int core) {
    uint32_t v;
    std::memcpy(&v, status + core * 0x34, 4);
    return v;
}
uint16_t envx(const uint8_t* status, int core, int n) {
    uint16_t v;
    std::memcpy(&v, status + core * 0x34 + 4 + 2 * n, 2);
    return v;
}

}  // namespace

int main() {
    // ADPCM: filter 0, shift 12 gives the nibbles themselves; the history carries over
    {
        uint8_t block[16] = {0x0C, 0, 0x21, 0xF3};
        int16_t out[28];
        int32_t s1 = 0, s2 = 0;
        decodeBlock(block, s1, s2, out);
        expect(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == -1 && out[4] == 0, "ADPCM nibbles, shift 12");
        expect(s1 == out[27] && s2 == out[26], "the history is the last two samples");
    }

    std::vector<uint8_t> ram(kRamBytes, 0);
    sample(ram, 0x5000, 8, 1 | 2);   // looping
    sample(ram, 0x6000, 4, 1);       // one-shot: ends after 4 blocks (112 samples at pitch 0x1000)

    // a one-shot voice: attack, the end of its sample, then off, ENDX set
    {
        Spu2 spu(ram.data());
        Image im;
        im.voice(0, 3, 0x1000, 0x6000, 0x000F, 0x0000);   // fastest linear attack
        im.keyOn(0, 3);
        spu.apply(im.bytes);
        uint8_t st[kStatusBytes];
        spu.status(st);
        expect(!(endx(st, 0) >> 3 & 1) && (endx(st, 0) >> 4 & 1), "keyed: ENDX clear for it, set for a voice that is off");
        spu.advance(50);
        spu.status(st);
        expect(envx(st, 0, 3) > 0 && spu.core[0].v[3].phase != kOff, "attack under way");
        spu.advance(100);
        spu.status(st);
        expect(spu.core[0].v[3].phase == kOff && envx(st, 0, 3) == 0 && (endx(st, 0) >> 3 & 1),
               "a one-shot sample ends: off, ENVX 0, ENDX set (112 samples)");
    }

    // a looping voice keeps sounding; key-off releases it to off
    {
        Spu2 spu(ram.data());
        Image im;
        im.voice(1, 0, 0x1000, 0x5000, 0x000F, 0x0008);   // fast attack; linear release, shift 8: about 512 samples
        im.keyOn(1, 0);
        spu.apply(im.bytes);
        spu.advance(48000);
        expect(spu.core[1].v[0].phase != kOff && (spu.core[1].endx & 1), "a loop sounds on, ENDX set at each loop end");
        Image off;
        off.keyOff(1, 0);
        spu.apply(off.bytes);
        expect(spu.core[1].v[0].phase == kRelease, "key-off: release");
        spu.advance(48000);
        expect(spu.core[1].v[0].phase == kOff, "released to off");
    }

    // D5's property: mixing or only advancing, the same timing and the same status at every frame, with voices keyed,
    // released, ending and looping at several pitches, the reverb on one core, and voices keyed again while sounding
    {
        Spu2 mixed(ram.data()), timed(ram.data());
        std::vector<int32_t> out(2 * 800);
        bool same = true, heard = false;
        int frames = 0;
        for (int f = 0; f < 600 && same; ++f, ++frames) {
            Image im;
            if (f == 0) {
                im.reverb(1, 3);
                for (int n = 0; n < 8; ++n) {
                    const int core = n & 1;
                    im.voice(core, n, static_cast<uint16_t>(0x0800 + 0x0300 * n), n % 3 ? 0x5000 : 0x6000,
                             static_cast<uint16_t>(0x0A0F + 0x0100 * n), static_cast<uint16_t>(0x1FC4 + n));
                    im.keyOn(core, n);
                }
            }
            if (f % 37 == 5) im.keyOn(f & 1, (f / 37) % 8);            // keyed again, sounding or not
            if (f % 53 == 20) im.keyOff((f + 1) & 1, (f / 53) % 8);    // released
            if (f % 101 == 50) {   // a pitch change
                im.voice(0, 2, static_cast<uint16_t>(0x0400 + f * 4), 0x5000, 0x0A0F, 0x1FC4);
            }
            mixed.apply(im.bytes);
            timed.apply(im.bytes);
            mixed.tick();
            timed.tick();
            std::fill(out.begin(), out.end(), 0);
            const int n = f % 3 == 2 ? 801 : 800;   // the frame clock's 800-sample frames, with an odd one
            out.resize(2 * n);
            mixed.mix(out.data(), n, false);
            timed.advance(n);
            for (int32_t s : out) heard = heard || s != 0;
            uint8_t a[kStatusBytes], b[kStatusBytes];
            mixed.status(a);
            timed.status(b);
            same = std::memcmp(a, b, sizeof a) == 0;
            for (int c = 0; c < 2 && same; ++c)
                for (int k = 0; k < 24 && same; ++k) {
                    const Voice &x = mixed.core[c].v[k], &y = timed.core[c].v[k];
                    same = x.phase == y.phase && x.env == y.env && x.env_wait == y.env_wait && x.block == y.block &&
                           x.loop == y.loop && x.index == y.index && x.counter == y.counter && x.flags == y.flags &&
                           x.s1 == y.s1 && x.s2 == y.s2 && x.last == y.last && x.cur == y.cur &&
                           std::memcmp(x.samples, y.samples, sizeof x.samples) == 0;
                }
        }
        expect(same, "mixing and advancing: the same voices (timing, samples) and status at every frame");
        expect(frames == 600 && heard, "600 frames, sound mixed");
        expect(mixed.counters.konTotal == timed.counters.konTotal && mixed.counters.konTotal > 8, "key-ons counted");
    }

    // the interpolations (audio.interpolation): on a sine of 0.1 and of 0.3 cycles a sample, each later one's error
    // is smaller (each at its own delay: linear between h[6] and h[7], cubic h[5] and h[6], sinc h[3] and h[4])
    {
        auto error = [](Interpolation mode, double cycles) {
            const double pi = 3.14159265358979323846;
            const int delay = mode == Interpolation::kLinear ? 1 : mode == Interpolation::kCubic ? 2 : 4;
            double worst = 0;
            for (int start = 0; start < 40; ++start)
                for (uint32_t frac = 0; frac < 4096; frac += 64) {
                    int16_t h[8];
                    for (int k = 0; k < 8; ++k)
                        h[k] = static_cast<int16_t>(std::lround(20000 * std::sin(2 * pi * cycles * (start + k))));
                    const double at = start + 7 - delay + frac / 4096.0;   // the point the output stands for
                    const double exact = 20000 * std::sin(2 * pi * cycles * at);
                    const double e = std::abs(interpolate(mode, h, frac) - exact);
                    if (e > worst) worst = e;
                }
            return worst;
        };
        for (double cycles : {0.1, 0.3}) {
            const double l = error(Interpolation::kLinear, cycles), c = error(Interpolation::kCubic, cycles),
                         s = error(Interpolation::kSinc, cycles);
            std::printf("  sine of %.1f cycles a sample, worst error: linear %.0f, cubic %.0f, sinc %.0f\n", cycles, l,
                        c, s);
            expect(c < l && s < c, "sinc closer than cubic, cubic closer than linear");
        }
        int16_t flat[8];
        std::fill(std::begin(flat), std::end(flat), int16_t{1234});
        expect(interpolate(Interpolation::kCubic, flat, 1000) == 1234 &&
                   interpolate(Interpolation::kSinc, flat, 3000) == 1234,
               "a constant stays the constant (unity gain)");
    }
    // the interpolation never changes the voices' timing: the status is the same, frame by frame, whatever is chosen
    {
        Spu2 lin(ram.data()), sinc(ram.data());
        sinc.interpolation = Interpolation::kSinc;
        std::vector<int32_t> out(2 * 800);
        bool same = true, differs = false;
        for (int f = 0; f < 200 && same; ++f) {
            Image im;
            if (f % 25 == 0)
                for (int n = 0; n < 4; ++n) {
                    im.voice(0, n, static_cast<uint16_t>(0x0C00 + 0x0500 * n), n % 2 ? 0x5000 : 0x6000, 0x0A0F, 0x1FC4);
                    im.keyOn(0, n);
                }
            lin.apply(im.bytes);
            sinc.apply(im.bytes);
            std::fill(out.begin(), out.end(), 0);
            lin.mix(out.data(), 800, false);
            std::vector<int32_t> other(2 * 800, 0);
            sinc.mix(other.data(), 800, false);
            differs = differs || out != other;
            uint8_t a[kStatusBytes], b[kStatusBytes];
            lin.status(a);
            sinc.status(b);
            same = std::memcmp(a, b, sizeof a) == 0;
        }
        expect(same && differs, "linear and sinc: the same status every frame, a different sound");
    }

    // the state handed over mid-run (the mixer setting switched): the side that loads it mixes exactly what the side
    // that saved it would have, reverb tail included; another layout is refused, changing nothing
    {
        Spu2 a(ram.data()), b(ram.data());
        auto frame = [&](Spu2& s, int f, std::vector<int32_t>& out) {
            Image im;
            if (f == 0) {
                im.reverb(0, 5);
                for (int n = 0; n < 6; ++n) {
                    im.voice(0, n, static_cast<uint16_t>(0x0900 + 0x0280 * n), n % 2 ? 0x5000 : 0x6000, 0x0A0F, 0x1FC6);
                    im.keyOn(0, n);
                }
            }
            if (f % 29 == 7) im.keyOn(0, f % 6);
            s.apply(im.bytes);
            out.assign(2 * 800, 0);
            s.mix(out.data(), 800, false);
        };
        std::vector<int32_t> oa, ob;
        for (int f = 0; f < 100; ++f) frame(a, f, oa);
        const std::vector<uint8_t> state = a.saveState();
        expect(b.loadState(state.data(), state.size()), "the state loads");
        bool same = true;
        for (int f = 100; f < 300 && same; ++f) {
            frame(a, f, oa);
            frame(b, f, ob);
            same = oa == ob;
        }
        expect(same, "after the hand-over: the same mix, sample for sample, for 200 frames");
        {   // a one-shot at the highest pitch, ended inside the pitch loop (its counter left above 0x1000): a live
            // state, taken (lane C's question on the bounds)
            Spu2 e(ram.data());
            Image im;
            im.voice(1, 7, 0x3FFF, 0x6000, 0x000F, 0x0000);
            im.keyOn(1, 7);
            e.apply(im.bytes);
            e.advance(200);
            const std::vector<uint8_t> s = e.saveState();
            Spu2 f(ram.data());
            expect(e.core[1].v[7].phase == kOff && e.core[1].v[7].counter >= 0x1000 && f.loadState(s.data(), s.size()),
                   "a voice that ended mid pitch loop: its state taken");
        }
        {   // a voice's sample index past its block: refused
            std::vector<uint8_t> wild = state;
            Voice v;
            std::memcpy(&v, wild.data() + 8, sizeof v);
            v.index = 28;
            std::memcpy(wild.data() + 8, &v, sizeof v);
            Spu2 d(ram.data());
            expect(!d.loadState(wild.data(), wild.size()), "a voice index past its block refused");
        }
        std::vector<uint8_t> bad = state;
        bad[4] ^= 1;   // another version
        Spu2 c(ram.data());
        const uint8_t before = static_cast<uint8_t>(c.core[0].v[0].phase);
        expect(!c.loadState(bad.data(), bad.size()) && !c.loadState(state.data(), state.size() - 1) &&
                   static_cast<uint8_t>(c.core[0].v[0].phase) == before,
               "another layout or a short state refused");
    }

    if (failures) return 1;
    std::printf("spu2: all checks passed\n");
    return 0;
}
