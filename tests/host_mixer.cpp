// platform/audio/host_mixer (PLAN.md D5) fed the records a game sends with audio.mixer = host (include/gt4/link/
// protocol.h GT4_REC_SPU2_*, stream 1), built here from a game-side Spu2: the host's output equals the game's own mix
// (its SPU2 plus BGM and movies), sample for sample; records before the state are not mixed; stream 0 passes through.
#include "host_mixer.h"
#include "gt4/link/protocol.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace gt4;

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
uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

std::vector<uint8_t> regs(uint32_t kind, uint32_t value, const uint8_t* data = nullptr, size_t bytes = 0) {
    std::vector<uint8_t> r(8 + bytes);
    put32(r.data(), kind);
    put32(r.data() + 4, value);
    if (bytes) std::memcpy(r.data() + 8, data, bytes);
    return r;
}

}  // namespace

int main() {
    std::vector<uint8_t> ram(spu::kRamBytes, 0);
    for (int b = 0; b < 64; ++b) {   // a looping sample at 0x4000
        uint8_t* p = ram.data() + 0x4000 + 16 * b;
        p[0] = 0x23;
        p[1] = static_cast<uint8_t>((b == 0 ? 4 : 0) | (b == 63 ? 3 : 0));
        for (int i = 2; i < 16; ++i) p[i] = static_cast<uint8_t>(b * 7 + i * 13);
    }
    std::vector<int16_t> heard;
    audio::HostMixer host([&](uint32_t rate, const int16_t* frames, size_t count) {
        if (rate == spu::kRate) heard.insert(heard.end(), frames, frames + 2 * count);
    });
    spu::Spu2 game(ram.data());

    auto image = [](int n, bool on, uint16_t pitch) {
        std::vector<uint8_t> im(spu::kImageBytes, 0);
        uint8_t* p = im.data() + (n & 1) * 0x1D0;
        put32(p + 4, 1u << n);
        uint8_t* r = p + 8 + 16 * n;
        put16(r, 0x3F);
        put16(r + 2, pitch);
        put16(r + 4, 0x3000);
        put16(r + 6, 0x2000);
        put32(r + 8, 0x4000);
        put16(r + 12, 0x0A0F);
        put16(r + 14, 0x1FC5);
        put32(p, rd32(p) | (on ? 0x800u : 0x1000u));
        put32(p + (on ? 0x1B4 : 0x1B8), 1u << n);
        return im;
    };

    // before the state: nothing is mixed (a capture that starts mid-run)
    {
        const std::vector<uint8_t> r = regs(GT4_SPU2_RUN, 800);
        host.record(GT4_REC_SPU2_REGS, r.data(), static_cast<uint32_t>(r.size()));
        expect(!host.mixing() && host.stats().unsynced == 1 && host.stats().samplesMixed == 0,
               "no state yet: not mixed");
    }
    // the switch: the whole RAM, then the state
    {
        std::vector<uint8_t> w(4 + spu::kRamBytes);
        std::memcpy(w.data() + 4, ram.data(), spu::kRamBytes);
        host.record(GT4_REC_SPU2_RAM, w.data(), static_cast<uint32_t>(w.size()));
        const std::vector<uint8_t> state = game.saveState();
        const std::vector<uint8_t> r = regs(GT4_SPU2_STATE, 0, state.data(), state.size());
        host.record(GT4_REC_SPU2_REGS, r.data(), static_cast<uint32_t>(r.size()));
        expect(host.mixing(), "the state taken");
    }
    // frames: the game applies images and mixes; the host gets the same records and adds stream 1
    bool same = true;
    std::vector<int16_t> expected;
    for (int f = 0; f < 240; ++f) {
        std::vector<std::vector<uint8_t>> images;
        if (f % 20 == 0) images.push_back(image((f / 20) % 6, true, static_cast<uint16_t>(0x0600 + f * 9)));
        if (f % 45 == 30) images.push_back(image((f / 45) % 6, false, 0x1000));
        for (const auto& im : images) {
            game.apply(im.data());
            const std::vector<uint8_t> r = regs(GT4_SPU2_IMAGE, 0, im.data(), im.size());
            host.record(GT4_REC_SPU2_REGS, r.data(), static_cast<uint32_t>(r.size()));
        }
        const int n = f % 3 == 2 ? 801 : 800;
        std::vector<int32_t> mix(2 * n, 0);
        game.mix(mix.data(), n, false);
        const std::vector<uint8_t> r = regs(GT4_SPU2_RUN, static_cast<uint32_t>(n));
        host.record(GT4_REC_SPU2_REGS, r.data(), static_cast<uint32_t>(r.size()));
        std::vector<int16_t> rest(2 * n);   // BGM and movies: a ramp
        for (int i = 0; i < 2 * n; ++i) rest[i] = static_cast<int16_t>((i * 37 + f * 101) % 4000 - 2000);
        host.audio(1, spu::kRate, rest.data(), static_cast<size_t>(n));
        for (int i = 0; i < 2 * n; ++i) {
            const int32_t s = mix[i] + rest[i];
            expected.push_back(static_cast<int16_t>(s < -32768 ? -32768 : s > 32767 ? 32767 : s));
        }
    }
    same = heard == expected;
    bool loud = false;
    for (int16_t s : heard) loud = loud || (s > 4000 || s < -4000);
    expect(same, "the host's output is the game's own mix, sample for sample");
    expect(loud && host.stats().unmatched == 0 && host.stats().images > 0,
           "voices heard, every stream 1 sample matched");

    // D9, audio.spatial: tags after every image. A second game and host pair, the same voices, three ways:
    // original with tags, stereo without tags (an older game) and stereo with tags.
    {
        // voice 0 (core 0) a car's, its group to the right and behind (x 0.6, z 0.8); voice 1 (core 1) untagged;
        // voice 2 (core 0) a car's without a direction
        std::vector<uint8_t> tags(GT4_SPU2_TAG_VOICES * sizeof(Gt4Spu2Tag), 0);
        auto* t = reinterpret_cast<Gt4Spu2Tag*>(tags.data());
        for (uint32_t i = 0; i < GT4_SPU2_TAG_VOICES; ++i) t[i].entrant = t[i].group = 0xFF;
        t[0] = Gt4Spu2Tag{2, 0, 1, 0, {0.6f, 0.0f, 0.8f}};
        t[2] = Gt4Spu2Tag{3, 1, 0, 0, {0.0f, 0.0f, 0.0f}};
        // the three voices keyed; voice 0 at VOLL 0x3000 and VOLR 0x7000 (0x6000 and -0x2000: GT4's rear inversion)
        auto keyed = [&](int n, uint16_t voll, uint16_t volr) {
            std::vector<uint8_t> im = image(n, true, 0x0800);
            uint8_t* r = im.data() + (n & 1) * 0x1D0 + 8 + 16 * n;
            put16(r + 4, voll);
            put16(r + 6, volr);
            return im;
        };
        struct Run {
            std::vector<int16_t> out;
            audio::HostMixer::Stats stats;
        };
        // flipAt: from that frame, voice 0's group is to the left instead (an image without a key-on, then its tags)
        // keyAtFlip: that image keys the voice on again (a new sound): no ramp
        auto session = [&](audio::Spatial mode, bool sendTags, int voices, int first = 0, int flipAt = -1,
                           bool keyAtFlip = false) {
            Run run;
            audio::HostMixer h([&](uint32_t, const int16_t* frames, size_t count) {
                run.out.insert(run.out.end(), frames, frames + 2 * count);
            });
            h.setSpatial(mode);
            spu::Spu2 g(ram.data());
            std::vector<uint8_t> w(4 + spu::kRamBytes);
            std::memcpy(w.data() + 4, ram.data(), spu::kRamBytes);
            h.record(GT4_REC_SPU2_RAM, w.data(), static_cast<uint32_t>(w.size()));
            const std::vector<uint8_t> state = g.saveState();
            const std::vector<uint8_t> st = regs(GT4_SPU2_STATE, 0, state.data(), state.size());
            h.record(GT4_REC_SPU2_REGS, st.data(), static_cast<uint32_t>(st.size()));
            for (int f = 0; f < 30; ++f) {
                if (f == 0) {
                    const uint16_t volumes[3][2] = {{0x3000, 0x7000}, {0x2800, 0x2800}, {0x1800, 0x1800}};
                    for (int n = first; n < voices; ++n) {
                        const std::vector<uint8_t> im = keyed(n, volumes[n][0], volumes[n][1]);
                        const std::vector<uint8_t> r = regs(GT4_SPU2_IMAGE, 0, im.data(), im.size());
                        h.record(GT4_REC_SPU2_REGS, r.data(), static_cast<uint32_t>(r.size()));
                        if (sendTags) {
                            const std::vector<uint8_t> tr = regs(GT4_SPU2_TAGS, GT4_SPU2_TAG_VOICES, tags.data(),
                                                                 tags.size());
                            h.record(GT4_REC_SPU2_REGS, tr.data(), static_cast<uint32_t>(tr.size()));
                        }
                    }
                }
                if (f == flipAt) {
                    std::vector<uint8_t> im = keyed(0, 0x3000, 0x7000);   // the same registers, no key-on unless asked
                    if (!keyAtFlip) {
                        put32(im.data(), rd32(im.data()) & ~0x800u);
                        put32(im.data() + 0x1B4, 0);
                    }
                    const std::vector<uint8_t> r = regs(GT4_SPU2_IMAGE, 0, im.data(), im.size());
                    h.record(GT4_REC_SPU2_REGS, r.data(), static_cast<uint32_t>(r.size()));
                    std::vector<uint8_t> moved = tags;
                    reinterpret_cast<Gt4Spu2Tag*>(moved.data())[0].direction[0] = -0.6f;
                    const std::vector<uint8_t> tr = regs(GT4_SPU2_TAGS, GT4_SPU2_TAG_VOICES, moved.data(),
                                                         moved.size());
                    h.record(GT4_REC_SPU2_REGS, tr.data(), static_cast<uint32_t>(tr.size()));
                }
                const std::vector<uint8_t> r = regs(GT4_SPU2_RUN, 800);
                h.record(GT4_REC_SPU2_REGS, r.data(), static_cast<uint32_t>(r.size()));
                std::vector<int16_t> silence(1600, 0);
                h.audio(1, spu::kRate, silence.data(), 800);
            }
            run.stats = h.stats();
            return run;
        };
        auto power = [](const std::vector<int16_t>& s, int channel) {
            double p = 0;
            for (size_t i = channel; i < s.size(); i += 2) p += double(s[i]) * s[i];
            return p;
        };
        const Run plain = session(audio::Spatial::kOriginal, false, 3);
        const Run original = session(audio::Spatial::kOriginal, true, 3);
        const Run older = session(audio::Spatial::kStereo, false, 3);
        const Run stereo = session(audio::Spatial::kStereo, true, 3);
        expect(original.out == plain.out, "original with tags is the D5 mix, bit for bit");
        expect(older.out == plain.out && older.stats.voicesTagged == 0 && older.stats.framesWithoutTags == 30,
               "stereo from an older game (no tags): every voice in the bed, the D5 mix");
        expect(stereo.stats.tagRecords == 3 && stereo.stats.voicesTagged == 30 && stereo.stats.voicesNoPosition == 30 &&
                   stereo.stats.voicesUntagged == 30 && stereo.stats.voicesPanned == 30,
               "the voices counted by their tags, a frame each");
        // the bed alone (the untagged voice and the one without a direction): stereo leaves it as original mixes it
        expect(session(audio::Spatial::kStereo, true, 3, 1).out == session(audio::Spatial::kOriginal, true, 3, 1).out,
               "the bed alone: stereo is original, bit for bit");
        // the car's voice alone: panned right at the power of its two volumes, no inverted side
        const Run carOriginal = session(audio::Spatial::kOriginal, true, 1);
        const Run carStereo = session(audio::Spatial::kStereo, true, 1);
        const double pl = power(carStereo.out, 0), pr = power(carStereo.out, 1);
        const double ol = power(carOriginal.out, 0), orr = power(carOriginal.out, 1);
        expect(pr > 3.0 * pl, "stereo: the car's voice is on the right, where its group is");
        expect(std::fabs((pl + pr) / (ol + orr) - 1.0) < 0.01, "stereo keeps the voice's power (GT4's level)");
        double corr = 0;
        for (size_t i = 0; i + 1 < carStereo.out.size(); i += 2)
            corr += double(carStereo.out[i]) * carStereo.out[i + 1];
        expect(corr > 0, "stereo: no side inverted");
        // a car crossing from right to left between two blocks: its left side ramps up across the block instead of
        // stepping at its edge (the first 50 samples still near the old gain, the last ones at the new)
        {
            const Run moving = session(audio::Spatial::kStereo, true, 1, 0, 10);
            auto rms = [&](size_t from, int channel) {
                double p = 0;
                for (size_t i = from; i < from + 50; ++i)
                    p += double(moving.out[2 * i + channel]) * moving.out[2 * i + channel];
                return std::sqrt(p / 50);
            };
            // the left against the right in each 50-sample window (the sample's own loudness cancels): about 0.33
            // before, still under 0.6 at the block's start with the ramp (3 without), about 3 at its end
            auto ratio = [&](size_t from) { return rms(from, 0) / std::max(rms(from, 1), 1.0); };
            const double before = ratio(10 * 800 - 50), start = ratio(10 * 800), end = ratio(11 * 800 - 50);
            std::printf("host_mixer: stereo crossing, left over right: %.2f before, %.2f at the block's start, %.2f "
                        "at its end\n", before, start, end);
            expect(before < 0.5 && start < 0.6 && end > 2.0, "stereo: a crossing car ramps across the block");
            // the same crossing with the voice keyed on again in that image: a new sound, at its gains from its
            // first samples (no ramp from the old sound's)
            const Run rekeyed = session(audio::Spatial::kStereo, true, 1, 0, 10, true);
            auto rekeyedRatio = [&](size_t from) {
                double l = 0, r = 0;
                for (size_t i = from; i < from + 50; ++i) {
                    l += double(rekeyed.out[2 * i]) * rekeyed.out[2 * i];
                    r += double(rekeyed.out[2 * i + 1]) * rekeyed.out[2 * i + 1];
                }
                return std::sqrt(l / std::max(r, 1.0));
            };
            std::printf("host_mixer: stereo crossing keyed on again, left over right %.2f at the block's start\n",
                        rekeyedRatio(10 * 800));
            expect(rekeyedRatio(10 * 800) > 2.0, "stereo: a voice keyed on where its gains change starts at them");
            const Run steady = session(audio::Spatial::kStereo, true, 1);
            expect(steady.out == carStereo.out, "stereo: a steady voice is not ramped (the same gains every block)");
        }
        // hrtf (step 3): an older game and the bed alone as original mixes them; the car's voice on its side, at its
        // power (the HRIR pairs are power-normalised; within 1.5 dB on this broadband sample)
        expect(session(audio::Spatial::kHrtf, false, 3).out == plain.out,
               "hrtf from an older game (no tags): every voice in the bed, the D5 mix");
        expect(session(audio::Spatial::kHrtf, true, 3, 1).out == session(audio::Spatial::kOriginal, true, 3, 1).out,
               "the bed alone: hrtf is original, bit for bit");
        const Run carHrtf = session(audio::Spatial::kHrtf, true, 1);
        const double hl = power(carHrtf.out, 0), hr = power(carHrtf.out, 1);
        std::printf("host_mixer: hrtf's car voice: right %.1f dB over left, %.2f dB against original's power; "
                    "%llu buses over %llu frames\n", 10 * std::log10(hr / hl), 10 * std::log10((hl + hr) / (ol + orr)),
                    static_cast<unsigned long long>(carHrtf.stats.hrtfBuses),
                    static_cast<unsigned long long>(carHrtf.stats.hrtfFrames));
        expect(hr > 2.0 * hl, "hrtf: the car's voice is louder on its side");
        expect(std::fabs(10 * std::log10((hl + hr) / (ol + orr))) < 1.5, "hrtf keeps the voice's power, within 1.5 dB");
        // the gains themselves
        const float right[3] = {1, 0, 0}, ahead[3] = {0, 0, -1}, none[3] = {0, 0, 0};
        const spu::Spu2::VoiceGain r = audio::stereoGain(0x6000, -0x2000, right);
        const spu::Spu2::VoiceGain a = audio::stereoGain(0x4000, 0x4000, ahead);
        const spu::Spu2::VoiceGain z = audio::stereoGain(0x4000, 0x4000, none);
        expect(r.on && r.left == 0 && std::abs(r.right - 25905) <= 1, "a voice to the right: all on the right");
        expect(a.left == a.right && std::abs(a.left - 0x4000) <= 1, "ahead: centred, the same gains as GT4's");
        expect(z.left == z.right, "no direction: centred");
    }

    // stream 0 (audio.mixer = game) goes to the output as it is
    {
        heard.clear();
        const int16_t frames[4] = {1, -2, 3, -4};
        host.audio(0, spu::kRate, frames, 2);
        expect(heard.size() == 4 && heard[0] == 1 && heard[3] == -4, "stream 0 passes through");
    }

    if (failures) return 1;
    std::printf("host_mixer: all checks passed\n");
    return 0;
}
