// The host's SPU2 mixer (host_mixer.h).
#include "host_mixer.h"

#include "gt4/link/protocol.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace gt4::audio {

namespace {

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void wavHeader(std::FILE* f, uint32_t bytes) {
    const uint32_t rate = spu::kRate;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0};
    const uint32_t riff = 36 + bytes, byteRate = rate * 4;
    std::memcpy(h + 4, &riff, 4);
    std::memcpy(h + 24, &rate, 4);
    std::memcpy(h + 28, &byteRate, 4);
    h[32] = 4;
    h[34] = 16;
    std::memcpy(h + 36, "data", 4);
    std::memcpy(h + 40, &bytes, 4);
    std::fseek(f, 0, SEEK_SET);
    std::fwrite(h, 1, sizeof h, f);
    std::fseek(f, 0, SEEK_END);
}

}  // namespace

HostMixer::HostMixer(Sink sink) : sink_(std::move(sink)), ram_(spu::kRamBytes, 0), spu_(ram_.data()) {
    const char* reverb = std::getenv("GT4_REVERB");   // the game's switch, the same on both sides
    dryOnly_ = reverb && !std::strcmp(reverb, "off");
    if (const char* path = std::getenv("GT4_HOST_AUDIO_WAV"); path && *path) {
        wav_ = std::fopen(path, "wb");
        if (wav_) wavHeader(wav_, 0);
    }
}

HostMixer::~HostMixer() {
    if (wav_) {
        wavHeader(wav_, wavBytes_);
        std::fclose(wav_);
    }
}

void HostMixer::emit(const int16_t* frames, size_t count) {
    stats_.framesOut += count;
    if (sink_) sink_(spu::kRate, frames, count);
    if (wav_) {
        std::fwrite(frames, 4, count, wav_);
        wavBytes_ += static_cast<uint32_t>(count * 4);
    }
}

void HostMixer::record(uint32_t type, const uint8_t* p, uint32_t size) {
    switch (type) {
    case GT4_REC_SPU2_RAM: {
        if (size < 4) return;
        const uint32_t at = rd32(p);
        if (at >= spu::kRamBytes) return;
        const uint32_t n = std::min<uint32_t>(size - 4, spu::kRamBytes - at);
        std::memcpy(ram_.data() + at, p + 4, n);
        ++stats_.ramWrites;
        stats_.ramBytes += n;
        return;
    }
    case GT4_REC_SPU2_REGS: {
        if (size < 8) return;
        const uint32_t kind = rd32(p), value = rd32(p + 4);
        const uint8_t* data = p + 8;
        const uint32_t bytes = size - 8;
        if (kind == GT4_SPU2_STATE) {
            synced_ = spu_.loadState(data, bytes);
            pending_.clear();
            ++stats_.states;
            return;
        }
        if (!synced_) {
            ++stats_.unsynced;
            return;
        }
        if (kind == GT4_SPU2_IMAGE && bytes >= spu::kImageBytes) {
            spu_.apply(data);
            ++stats_.images;
        } else if (kind == GT4_SPU2_RUN && value > 0 && value <= spu::kRate) {
            scratch_.assign(2 * static_cast<size_t>(value), 0);
            spu_.tick();
            spu_.interpolation = static_cast<spu::Interpolation>(interpolation_.load());
            spu_.mix(scratch_.data(), static_cast<int>(value), dryOnly_);
            pending_.insert(pending_.end(), scratch_.begin(), scratch_.end());
            stats_.samplesMixed += value;
        }
        return;
    }
    default:
        return;
    }
}

void HostMixer::audio(uint32_t stream, uint32_t rate, const int16_t* frames, size_t count) {
    if (rate != spu::kRate) return;
    if (stream == 0) {   // the game's own mix (audio.mixer = game)
        emit(frames, count);
        return;
    }
    if (stream != 1) return;
    // BGM and movies: added to the SPU2's samples of the same frame
    out_.resize(2 * count);
    for (size_t i = 0; i < 2 * count; ++i) {
        int32_t s = frames[i];
        if (!pending_.empty()) {
            s += pending_.front();
            pending_.pop_front();
        } else {
            ++stats_.unmatched;
        }
        out_[i] = static_cast<int16_t>(std::clamp(s, -32768, 32767));
    }
    emit(out_.data(), count);
}

}  // namespace gt4::audio
