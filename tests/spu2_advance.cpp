// platform/audio/spu/spu2.cpp's advance() (PLAN.md D5: the game's voice model while the host mixes) against the
// per-sample path, bit for bit. The oracle is mix(), whose voices still step one sample at a time through the same
// voiceSample and envelope() advance() used before it took the bookkeeping calls in bulk: mixing changes no voice
// state. Each case puts one voice in a state, runs mix(n) on one Spu2 and advance(n) on another, and compares every
// Voice field and ENDX, for every n from 1 to 160 (so at every sample) and at 400, 800 and 1600. The states cover the
// envelope's whole space (attack, decay, sustain and release; linear and exponential; increase and decrease; every
// shift and step from random ADSR registers; start levels at the edges, 0x6000 included where the exponential
// increase slows; a sustain held at its clamp, which steps without moving; waits from 0 up) and the voice's (pitch
// 0, below, at and above one sample a call, past 0x3FFF's clamp; any index and counter; blocks with no flag, loop
// start, loop end with repeat, end without repeat, in random ADPCM). Key-on and key-off come from apply() between calls, so their states are among these (attack from level 0,
// release from any level).
#include "spu/spu2.h"

#include <algorithm>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace gt4::spu;

namespace {

int failures = 0;

bool same(const Voice& a, const Voice& b) {
    return a.pitch == b.pitch && a.voll == b.voll && a.volr == b.volr && a.adsr1 == b.adsr1 && a.adsr2 == b.adsr2 &&
           a.ssa == b.ssa && a.phase == b.phase && a.env == b.env && a.env_wait == b.env_wait && a.block == b.block &&
           a.loop == b.loop && a.flags == b.flags && a.index == b.index && a.counter == b.counter &&
           std::memcmp(a.samples, b.samples, sizeof a.samples) == 0 && a.s1 == b.s1 && a.s2 == b.s2 &&
           a.last == b.last && a.cur == b.cur && std::memcmp(a.hist, b.hist, sizeof a.hist) == 0;
}

void report(int c, int n, const Voice& want, const Voice& got, uint32_t endxWant, uint32_t endxGot) {
    if (++failures > 10) return;
    std::fprintf(stderr,
                 "FAILED: case %d after %d samples: want phase %d env %d wait %d block %x index %d counter %x endx %x;"
                 " got phase %d env %d wait %d block %x index %d counter %x endx %x\n",
                 c, n, want.phase, want.env, want.env_wait, want.block, want.index, want.counter, endxWant, got.phase,
                 got.env, got.env_wait, got.block, got.index, got.counter, endxGot);
}

}  // namespace

int main() {
    std::mt19937 rng(20261109);
    auto pick = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(rng); };

    // SPU RAM: random ADPCM blocks (any shift and filter), their flags from a pattern: mostly none, some loop starts,
    // loop ends that repeat, and ends that stop the voice
    std::vector<uint8_t> ram(2u << 20);
    for (auto& b : ram) b = static_cast<uint8_t>(pick(0, 255));
    for (size_t at = 0; at < ram.size(); at += 16) {
        const uint32_t r = pick(0, 99);
        ram[at + 1] = r < 70 ? 0 : r < 80 ? 4 : r < 92 ? 3 : 1;
    }

    const Phase phases[] = {kAttack, kDecay, kSustain, kRelease};
    const int32_t levels[] = {0, 1, 0x10, 0x800, 0x3000, 0x5FFF, 0x6000, 0x6001, 0x7000, 0x7FFE, 0x7FFF};
    const uint16_t pitches[] = {0, 1, 0x7FF, 0xFFF, 0x1000, 0x1001, 0x1FFF, 0x2000, 0x3FFF, 0x4000, 0xFFFF};
    std::vector<int> lengths;
    for (int n = 1; n <= 160; ++n) lengths.push_back(n);
    lengths.insert(lengths.end(), {400, 800, 1600});

    int cases = 0;
    std::vector<int32_t> mixed(2 * 1600);
    for (int c = 0; c < 1200; ++c) {
        Voice v;
        v.pitch = c < 2 * 11       ? pitches[c % 11]
                  : pick(0, 3) == 0 ? pitches[pick(0, 10)]
                                    : static_cast<uint16_t>(pick(0, 0x4000));
        v.adsr1 = static_cast<uint16_t>(pick(0, 0xFFFF));
        v.adsr2 = static_cast<uint16_t>(pick(0, 0xFFFF));
        v.phase = phases[c % 4];
        v.env = pick(0, 1) ? levels[pick(0, 10)] : static_cast<int32_t>(pick(0, 0x7FFF));
        if (v.phase == kSustain && c % 8 == 2) {   // a sustain held at its clamp (a race's engines), every rate
            v.env = (v.adsr2 >> 14) & 1 ? 0 : 0x7FFF;
        }
        const uint32_t w = pick(0, 9);
        v.env_wait = w < 4 ? static_cast<int32_t>(w) : w < 8 ? static_cast<int32_t>(pick(0, 300))
                                                              : static_cast<int32_t>(pick(0, 1u << 20));
        v.block = pick(0, (2u << 20) / 16 - 1) * 16;
        v.loop = pick(0, (2u << 20) / 16 - 1) * 16;
        v.flags = ram[v.block + 1];
        v.index = static_cast<int>(pick(0, 27));
        v.counter = pick(0, 0xFFF);
        for (auto& s : v.samples) s = static_cast<int16_t>(pick(0, 0xFFFF));
        v.s1 = static_cast<int32_t>(pick(0, 0xFFFF)) - 0x8000;
        v.s2 = static_cast<int32_t>(pick(0, 0xFFFF)) - 0x8000;
        v.last = static_cast<int16_t>(pick(0, 0xFFFF));
        v.cur = v.samples[v.index];
        for (auto& h : v.hist) h = static_cast<int16_t>(pick(0, 0xFFFF));
        v.hist[7] = v.cur;
        const int core = static_cast<int>(pick(0, 1)), slot = static_cast<int>(pick(0, 23));
        for (int n : lengths) {
            Spu2 oracle(ram.data()), fast(ram.data());
            oracle.core[core].v[slot] = v;
            fast.core[core].v[slot] = v;
            std::fill(mixed.begin(), mixed.begin() + 2 * n, 0);
            oracle.mix(mixed.data(), n, true);   // (dry only: no reverb state, which advance() never has)
            fast.advance(n);
            const Voice &want = oracle.core[core].v[slot], &got = fast.core[core].v[slot];
            if (!same(want, got) || oracle.core[core].endx != fast.core[core].endx) {
                report(c, n, want, got, oracle.core[core].endx, fast.core[core].endx);
                break;
            }
        }
        ++cases;
    }
    if (failures) {
        std::fprintf(stderr, "spu2_advance: %d of %d cases differ\n", failures, cases);
        return 1;
    }
    std::printf("spu2_advance: %d cases, every Voice field and ENDX identical at every sample\n", cases);
    return 0;
}
