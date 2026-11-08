#pragma once
// The host's SPU2 mixer (PLAN.md D5): with audio.mixer = host, the game sends its SPU2's register images, SPU RAM
// writes and each frame's sample count (include/gt4/link/protocol.h GT4_REC_SPU2_*), and BGM and movie sound as audio
// stream 1; this runs the same SPU2 (platform/audio/spu, one source with the game) and plays its mix with stream 1
// added. With audio.mixer = game, the game's own mix (stream 0) goes to the output as before. GT4's status always
// comes from the game's SPU2 (spikes/R16-audio-reply-timing.md): this side's latency only changes when a sound is
// heard.
//
// GT4_HOST_AUDIO_WAV=path writes what goes to the output to a WAV file (with the game's GT4_AUDIO_WAV, the two mixes of
// one run compared: D5's -60 dB check). Not thread-safe: one thread feeds it (the GS thread's record dispatch).
#include "spu/spu2.h"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <vector>

namespace gt4::audio {

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
        uint64_t unsynced = 0;    // SPU2 records before a state: not mixed (a capture started mid-run)
        uint64_t unmatched = 0;   // stream 1 values (L or R) without an SPU2 value to add them to (sent alone)
    };
    const Stats& stats() const { return stats_; }
    bool mixing() const { return synced_; }

private:
    void emit(const int16_t* frames, size_t count);
    Sink sink_;
    std::vector<uint8_t> ram_;
    spu::Spu2 spu_;
    bool synced_ = false;           // a state has arrived (the game's switch to host): the voices are the game's
    std::deque<int32_t> pending_;   // the SPU2's mixed samples (L, R) waiting for stream 1's of the same frame
    std::vector<int32_t> scratch_;
    std::vector<int16_t> out_;
    bool dryOnly_ = false;
    std::FILE* wav_ = nullptr;
    uint32_t wavBytes_ = 0;
    Stats stats_;
};

}  // namespace gt4::audio
