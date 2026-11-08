#pragma once
// The host's SPU2 mixer (PLAN.md D5): with audio.mixer = host, the game sends its SPU2's register images, SPU RAM
// writes and each frame's sample count (include/gt4/link/protocol.h GT4_REC_SPU2_*), and BGM and movie sound as audio
// stream 1; this runs the same SPU2 (platform/audio/spu, one source with the game) and plays its mix with stream 1
// added. With audio.mixer = game, the game's own mix (stream 0) goes to the output as before. GT4's status always
// comes from the game's SPU2 (spikes/R16-audio-reply-timing.md): this side's latency only changes when a sound is
// heard.
//
// audio.spatial (PLAN.md D9): the game tags each voice after every register image (GT4_SPU2_TAGS: the car whose
// sound it is and that sound group's direction, GT4's own, from its listener, the player's car). `stereo` pans a
// tagged voice with equal power from that direction, at the power sum of GT4's VOLL / VOLR: GT4's level and distance
// gain kept, its pan and its phase inversion behind the listener dropped. Untagged voices, voices without a
// direction, voices in sweep mode, and every voice when the image came without tags (an older game) are the bed,
// mixed as `original` mixes them. `original`: no voice changed, the D5 mix bit for bit.
//
// GT4_TRACE_D9 (debugging, D9's direction and distance checks): a line a frame mixed and car, with the count of
// register images seen so far (the game's GT4_TRACE_D9=3 lines carry the same count): the car's tagged voices' level
// left and right as mixed, and at GT4's own volumes (the registers' power sum), in dB of full scale times envelope.
//
// GT4_HOST_AUDIO_WAV=path writes what goes to the output to a WAV file (with the game's GT4_AUDIO_WAV, the two mixes of
// one run compared: D5's -60 dB check). Not thread-safe: one thread feeds it (the GS thread's record dispatch).
#include "gt4/link/protocol.h"
#include "spu/spu2.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <vector>

namespace gt4::audio {

enum class Spatial : uint8_t { kOriginal, kStereo };   // audio.spatial

// A tagged voice's gains under `stereo` (Q15): the power sum of the registers' volumes `left` and `right` (signs
// dropped), panned with equal power from `direction` (x right, z ahead negative; only the left-right part counts).
spu::Spu2::VoiceGain stereoGain(int32_t left, int32_t right, const float direction[3]);

class HostMixer {
public:
    // Output: interleaved stereo s16 frames at `rate` Hz.
    using Sink = std::function<void(uint32_t rate, const int16_t* frames, size_t count)>;
    explicit HostMixer(Sink sink);
    ~HostMixer();
    HostMixer(const HostMixer&) = delete;
    HostMixer& operator=(const HostMixer&) = delete;

    // A game -> host record of the SPU2: GT4_REC_SPU2_REGS or GT4_REC_SPU2_RAM (others are ignored).
    void record(uint32_t type, const uint8_t* payload, uint32_t size);
    // The game's sound (GT4_REC_AUDIO): stream 0 its own mix, stream 1 BGM and movies to add to this side's mix.
    void audio(uint32_t stream, uint32_t rate, const int16_t* frames, size_t count);

    struct Stats {
        uint64_t images = 0, ramWrites = 0, ramBytes = 0, states = 0, samplesMixed = 0, framesOut = 0;
        uint64_t runs = 0;        // frames mixed (GT4_SPU2_RUN records)
        uint64_t unsynced = 0;    // SPU2 records before a state: not mixed (a capture started mid-run)
        uint64_t unmatched = 0;   // stream 1 values (L or R) without an SPU2 value to add them to (sent alone)
        // D9: GT4_SPU2_TAGS records and their bytes; voices playing, summed over the frames mixed, by their tags
        // (tagged with a direction, untagged, tagged without one; none counted while the image came without tags)
        uint64_t tagRecords = 0, tagBytes = 0, framesWithoutTags = 0;
        uint64_t voicesTagged = 0, voicesUntagged = 0, voicesNoPosition = 0;
        uint64_t voicesPanned = 0;   // the tagged voices `stereo` panned (not in sweep mode)
        double mixMicroseconds = 0, mixMicrosecondsMax = 0;   // the SPU2's mix of a frame (PERF)
    };
    // audio.interpolation (any thread; the next frame mixed uses it). kLinear, the original, unless set.
    void setInterpolation(spu::Interpolation mode) { interpolation_.store(static_cast<uint8_t>(mode)); }
    // audio.spatial (any thread; the next frame mixed uses it). kOriginal unless set.
    void setSpatial(Spatial mode) { spatial_.store(static_cast<uint8_t>(mode)); }

    const Stats& stats() const { return stats_; }
    bool mixing() const { return synced_; }

private:
    void emit(const int16_t* frames, size_t count);
    void spatialGains();   // the next frame's spu_.gains from the tags (audio.spatial) and the voice counters
    Sink sink_;
    std::vector<uint8_t> ram_;
    spu::Spu2 spu_;
    bool synced_ = false;           // a state has arrived (the game's switch to host): the voices are the game's
    std::deque<int32_t> pending_;   // the SPU2's mixed samples (L, R) waiting for stream 1's of the same frame
    std::vector<int32_t> scratch_;
    std::vector<int16_t> out_;
    bool dryOnly_ = false;
    std::atomic<uint8_t> interpolation_{static_cast<uint8_t>(spu::Interpolation::kLinear)};
    std::atomic<uint8_t> spatial_{static_cast<uint8_t>(Spatial::kOriginal)};
    Gt4Spu2Tag tags_[GT4_SPU2_TAG_VOICES] = {};
    bool tagsFresh_ = false;   // tags_ came after the last image (else that image's voices are all the bed)
    uint64_t imagesSeen_ = 0;  // every GT4_SPU2_IMAGE, before the state too (GT4_TRACE_D9's join with the game's)
    bool trace_ = false;
    std::array<spu::Spu2::VoiceGain, GT4_SPU2_TAG_VOICES> gains_{};
    std::FILE* wav_ = nullptr;
    uint32_t wavBytes_ = 0;
    Stats stats_;
};

}  // namespace gt4::audio
