#pragma once
// The PS2's sound processor (SPU2) as GT4 drives it: the register image its sound driver sends every VBlank (SPUP
// fno 4), the voices it keys from SPU RAM (SPU ADPCM, pitch, ADSR, volumes), the reverb unit, and the status the driver
// reads back (ENDX, ENVX). Written for this project from the public SPU documentation (psx-spx: "SPU ADPCM",
// "SPU ADSR", "SPU Reverb Formula", "Reverb Examples").
//
// One source for the game and the host (PLAN.md D5, spikes/R16-audio-reply-timing.md): the game answers GT4's status
// from it in the same SPUP call, the host mixes the same voices. Both step the voices with run(): advance() keeps the
// voices' whole state (key state, envelope, address, ENDX, the decoded samples), mix() also applies the volumes and
// the reverb and adds the output. A voice ends at the same sample on both sides, and either side's state can be
// handed to the other (saveState). Pure C++: no SDL, no runtime calls.
#include <cstdint>
#include <vector>

namespace gt4::spu {

constexpr int kRate = 48000;            // output samples a second (0x1000 pitch = one SPU RAM sample each)
constexpr uint32_t kRamBytes = 2u << 20;
constexpr uint32_t kImageBytes = 2 * 0x1D0;   // SPUP fno 4: one 0x1D0-byte image per core
constexpr uint32_t kStatusBytes = 2 * 0x34;   // per core: u32 ENDX, u16 ENVX[24]

// SPU ADPCM: one 16-byte block (shift/filter, flags, 14 bytes of nibbles) to 28 samples, with the two history samples.
void decodeBlock(const uint8_t* block, int32_t& s1, int32_t& s2, int16_t out[28]);

enum Phase { kOff, kAttack, kDecay, kSustain, kRelease };

// How a voice's samples are interpolated between SPU RAM samples (audio.interpolation, the host's mixer only; the game
// always mixes with kLinear). kLinear: between the two newest samples, as this project always did (original). kCubic:
// a 4-point Catmull-Rom spline, one sample later. kSinc: an 8-point Blackman-windowed sinc, four samples later. The
// later ones are a fixed delay of 1 or 4 SPU RAM samples (under 0.1 ms at pitch 0x1000). None of them changes a
// voice's timing, so the status and the game's voices are the same whatever is chosen.
enum class Interpolation : uint8_t { kLinear, kCubic, kSinc };
// One interpolated sample from a voice's last 8 samples (hist, the newest last) at the 12-bit fraction `frac`.
int32_t interpolate(Interpolation mode, const int16_t hist[8], uint32_t frac);

struct Voice {
    uint16_t pitch = 0, voll = 0, volr = 0, adsr1 = 0, adsr2 = 0;   // registers
    uint32_t ssa = 0;
    // what the status depends on
    Phase phase = kOff;
    int32_t env = 0, env_wait = 0;
    uint32_t block = 0, loop = 0;   // byte addresses in SPU RAM
    uint8_t flags = 0;              // the current block's loop flags
    int index = 0;                  // sample in the block
    uint32_t counter = 0;           // 12-bit fraction
    // the decoded block and the interpolation's two samples
    int16_t samples[28] = {};
    int32_t s1 = 0, s2 = 0;
    int16_t last = 0, cur = 0;
    int16_t hist[8] = {};   // the last 8 decoded samples, the newest (cur) last: the finer interpolations' taps
};

// The reverb unit (psx-spx "SPU Reverb Formula"): the PS1's, at half the output rate, on a work area of its own. GT4
// picks one of libsd's presets (sceSdSetEffectAttr: mode, depth, delay, feedback).
struct Reverb {
    bool on = false;
    int mode = -1;
    int32_t depth[2] = {0, 0};
    int32_t r[32] = {};
    std::vector<int16_t> buf = std::vector<int16_t>(16);
    uint32_t pos = 0;
    int32_t in[2] = {0, 0};   // the wet input of the first half of the current 24 kHz step
    int32_t out[2] = {0, 0}, prev[2] = {0, 0};
    bool odd = false;

    void set(int mode, int delay, int feedback);
    // One 48 kHz output sample: the wet input in, the reverberated pair added to outl / outr.
    void sample(int32_t inl, int32_t inr, int32_t& outl, int32_t& outr);

private:
    void step(int32_t inl, int32_t inr);
    int16_t& at(int32_t reg, int32_t back = 0);
};

struct Core {
    Voice v[24];
    uint32_t endx = 0;
    uint32_t vmixl = 0xFFFFFF, vmixr = 0xFFFFFF, vmixel = 0, vmixer = 0;
    int16_t evol[2] = {0, 0};
    Reverb fx;
    uint8_t effect[0x10] = {};   // the effect attributes last applied
};

class Spu2 {
public:
    // `ram`: the 2 MiB of SPU RAM, owned by the caller (the game fills it through STRP).
    explicit Spu2(const uint8_t* ram) : ram_(ram) {}

    // SPUP fno 4's register image (kImageBytes): registers, effect attributes, volumes, key-offs then key-ons.
    void apply(const uint8_t* image);
    // The status GT4's driver reads (kStatusBytes): per core ENDX (a voice that is off counts as ended), ENVX.
    void status(uint8_t* reply) const;
    // `n` output samples: the voices advanced, nothing mixed.
    void advance(int n);
    // `n` output samples mixed and added to `stereo` (2n values); the same timing as advance. `dryOnly`: the effect
    // sends come back dry at the effect return volume instead of through the reverb.
    void mix(int32_t* stereo, int n, bool dryOnly);
    // mix()'s interpolation (the host's audio.interpolation); kLinear unless set.
    Interpolation interpolation = Interpolation::kLinear;
    // mix()'s volumes in place of a voice's VOLL / VOLR (the host's audio.spatial, PLAN.md D9), by hardware voice
    // (core * 24 + voice): `on` takes `left` and `right` (Q15, as the registers' volumes) for the dry and the effect
    // sends alike; with `ramp`, the gains go linearly from leftFrom / rightFrom to left / right across the block, so a
    // voice whose gains change between blocks doesn't step at the block's edge. With routes > 0 (the host's hrtf),
    // the voice's dry sound goes instead to up to four mono buses (bus[r], 1..busCount), each at a gain ramped
    // linearly across the block from from[r] to to[r], and its effect sends ramp from wetLeftFrom / wetRightFrom to
    // wetLeft / wetRight; with `ramp` too, its dry sound also goes to the stereo mix, ramped from leftFrom / rightFrom
    // to left / right (a voice crossing between the bed and the buses fades from one to the other). Null (the game,
    // audio.spatial = original): every voice mixes with its registers, as always.
    struct VoiceGain {
        bool on = false;
        int32_t left = 0, right = 0;
        bool ramp = false;
        int32_t leftFrom = 0, rightFrom = 0;
        uint8_t routes = 0;
        uint8_t bus[4] = {0, 0, 0, 0};
        int32_t from[4] = {0, 0, 0, 0}, to[4] = {0, 0, 0, 0};
        int32_t wetLeft = 0, wetRight = 0, wetLeftFrom = 0, wetRightFrom = 0;
    };
    const VoiceGain* gains = nullptr;
    // mix()'s mono buses (VoiceGain::bus): busCount runs of n samples each, bus b at buses + (b - 1) * n, which mix()
    // adds to; the caller clears them and reads them after.
    int32_t* buses = nullptr;
    int busCount = 0;
    // The next frame (the frame clock's): the key-on gaps below count frames.
    void tick() { ++counters.frame; }

    // The voices', the cores' and the reverbs' whole state (not SPU RAM, not the counters): another Spu2 that loads it
    // goes on exactly as this one would. loadState returns false, changing nothing, for bytes of another layout.
    std::vector<uint8_t> saveState() const;
    bool loadState(const uint8_t* data, size_t size);

    Core core[2];

    // Counters (GT4_TRACE_SPU, R16's GT4_SPU_REPLY_DELAY): key-ons and key-offs, since the reader last cleared
    // kon / koff; key-ons onto a voice still keyed or releasing; key-ons 1-4 frames after the same voice's previous one.
    struct Counters {
        uint32_t kon = 0, koff = 0;
        uint64_t konTotal = 0, konKeyed = 0, konReleasing = 0, konAgain[5] = {};
        uint32_t frame = 0, konFrame[2][24] = {};
    } counters;

    // Called when an effect attribute set arrives with its "set" byte (core, the 0x10 bytes), for a log line.
    void (*onEffect)(int core, const Reverb& fx, const uint8_t* attributes) = nullptr;

private:
    const uint8_t* ram_;
    void keyOn(Voice& v);
    void loadBlock(Voice& v);
    template <bool kMix> int32_t voiceSample(Core& core, int n, Voice& v);
    template <bool kMix> void run(int32_t* stereo, int n, bool dryOnly);
    // advance()'s voice: what n calls of voiceSample<false> and envelope() do, with the calls that are pure
    // bookkeeping (no block crossing, no envelope step) taken in bulk (spu2.cpp)
    void advanceVoice(Core& core, int n, Voice& v, int count);
};

void envelope(Voice& v);   // one step of the ADSR envelope generator
// A VOLL / VOLR register's volume as mix() applies it (Q15; sweep mode, not modelled, at half volume).
int32_t volume(uint16_t reg);

}  // namespace gt4::spu
