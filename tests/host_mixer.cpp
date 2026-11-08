// platform/audio/host_mixer (PLAN.md D5) fed the records a game sends with audio.mixer = host (include/gt4/link/
// protocol.h GT4_REC_SPU2_*, stream 1), built here from a game-side Spu2: the host's output equals the game's own mix
// (its SPU2 plus BGM and movies), sample for sample; records before the state are not mixed; stream 0 passes through.
#include "host_mixer.h"
#include "gt4/link/protocol.h"

#include <cstdio>
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
