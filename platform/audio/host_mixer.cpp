// The host's SPU2 mixer (host_mixer.h).
#include "host_mixer.h"

#include "gt4/link/protocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

spu::Spu2::VoiceGain stereoGain(int32_t left, int32_t right, const float direction[3]) {
    const double power = std::sqrt(double(left) * left + double(right) * right);
    const double x = direction[0], z = direction[2], length = std::sqrt(x * x + z * z);
    // the left-right part of the direction, -1 (left) to 1 (right); ahead and behind alike (no rear inversion)
    const double pan = length > 0.0 && std::isfinite(length) ? std::clamp(x / length, -1.0, 1.0) : 0.0;
    const double angle = (pan + 1.0) * 0.78539816339744831;   // 0 .. pi / 2
    auto q15 = [](double g) { return static_cast<int32_t>(std::lround(std::min(g, 32767.0))); };
    spu::Spu2::VoiceGain g;
    g.on = true;
    g.left = q15(power * std::cos(angle));
    g.right = q15(power * std::sin(angle));
    return g;
}

HostMixer::HostMixer(Sink sink) : sink_(std::move(sink)), ram_(spu::kRamBytes, 0), spu_(ram_.data()) {
    const char* reverb = std::getenv("GT4_REVERB");   // the game's switch, the same on both sides
    dryOnly_ = reverb && !std::strcmp(reverb, "off");
    const char* trace = std::getenv("GT4_TRACE_D9");
    trace_ = trace && *trace && *trace != '0';
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
        if (kind == GT4_SPU2_IMAGE) ++imagesSeen_;
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
            tagsFresh_ = false;   // until this image's tags come (never, from an older game)
        } else if (kind == GT4_SPU2_TAGS && value == GT4_SPU2_TAG_VOICES && bytes >= sizeof tags_) {
            std::memcpy(tags_, data, sizeof tags_);
            tagsFresh_ = true;
            ++stats_.tagRecords;
            stats_.tagBytes += size;
        } else if (kind == GT4_SPU2_RUN && value > 0 && value <= spu::kRate) {
            scratch_.assign(2 * static_cast<size_t>(value), 0);
            spu_.tick();
            spu_.interpolation = static_cast<spu::Interpolation>(interpolation_.load());
            spatialGains();
            const auto t0 = std::chrono::steady_clock::now();
            spu_.mix(scratch_.data(), static_cast<int>(value), dryOnly_);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            stats_.mixMicroseconds += us;
            stats_.mixMicrosecondsMax = std::max(stats_.mixMicrosecondsMax, us);
            if (trace_) std::printf("d9b: %u\n", value);   // (the blocks' sizes: where their edges fall in the output)
            pending_.insert(pending_.end(), scratch_.begin(), scratch_.end());
            stats_.samplesMixed += value;
            ++stats_.runs;
        }
        return;
    }
    default:
        return;
    }
}

void HostMixer::spatialGains() {
    spu_.gains = nullptr;
    const bool stereo = static_cast<Spatial>(spatial_.load()) == Spatial::kStereo;
    if (!tagsFresh_) ++stats_.framesWithoutTags;   // an older game, or an image whose tags didn't come: all the bed
    bool any = false;
    // (a voice keyed on since the last block: spu2 stamps its key-on with the frame before this block's tick)
    const uint32_t keyedFrame = spu_.counters.frame - 1;
    for (uint32_t hw = 0; hw < GT4_SPU2_TAG_VOICES; ++hw) {
        gains_[hw].on = false;
        Last& last = last_[hw];
        const Last before = last;
        last = Last{};
        const spu::Voice& v = spu_.core[hw / 24].v[hw % 24];
        if (v.phase == spu::kOff) continue;
        const Gt4Spu2Tag& t = tags_[hw];
        bool panned = false;
        if (tagsFresh_) {
            if (t.entrant == 0xFF) {
                ++stats_.voicesUntagged;
            } else if (!(t.flags & 1)) {
                ++stats_.voicesNoPosition;
            } else {
                ++stats_.voicesTagged;
                panned = stereo && !(v.voll & 0x8000) && !(v.volr & 0x8000);   // (sweep mode: the bed)
            }
        }
        if (!stereo) continue;   // (original: no voice changed)
        // where the voice ends this block: panned, or its registers (the bed)
        spu::Spu2::VoiceGain g;
        if (panned)
            g = stereoGain(static_cast<int16_t>(v.voll << 1), static_cast<int16_t>(v.volr << 1), t.direction);
        else
            g.left = spu::volume(v.voll), g.right = spu::volume(v.volr);
        last = Last{true, panned, g.left, g.right};
        // from where it was last block, unless it was keyed on since (a new sound starts at its gains)
        const bool keyed = spu_.counters.konFrame[hw / 24][hw % 24] == keyedFrame;
        const bool from = before.on && !keyed && (before.left != g.left || before.right != g.right);
        if (!panned && !(before.panned && from)) continue;   // the bed, as original mixes it
        g.on = true;
        if (from) {
            g.ramp = true;
            g.leftFrom = before.left;
            g.rightFrom = before.right;
        }
        gains_[hw] = g;
        if (panned) ++stats_.voicesPanned;
        any = true;
    }
    if (any) spu_.gains = gains_.data();
    if (!trace_) return;
    // each car's tagged voices: the power left and right as mixed, and at GT4's volumes (envelope included)
    double mixed[8][2] = {}, gt4[8] = {};
    int voices[8] = {};
    for (uint32_t hw = 0; hw < GT4_SPU2_TAG_VOICES; ++hw) {
        const spu::Voice& v = spu_.core[hw / 24].v[hw % 24];
        const Gt4Spu2Tag& t = tags_[hw];
        if (v.phase == spu::kOff || t.entrant >= 8 || !(t.flags & 1)) continue;
        const double e = v.env / 32768.0;
        const double l = static_cast<int16_t>(v.voll << 1) / 32768.0, r = static_cast<int16_t>(v.volr << 1) / 32768.0;
        const bool panned = gains_[hw].on;
        const double ml = panned ? gains_[hw].left / 32768.0 : l, mr = panned ? gains_[hw].right / 32768.0 : r;
        mixed[t.entrant][0] += e * e * ml * ml;
        mixed[t.entrant][1] += e * e * mr * mr;
        gt4[t.entrant] += e * e * (l * l + r * r);
        ++voices[t.entrant];
    }
    auto db = [](double p) { return p > 1e-12 ? 10.0 * std::log10(p) : -120.0; };
    for (int c = 0; c < 8; ++c)
        if (voices[c])
            std::printf("d9h: img %llu frame %llu entrant %d voices %d left %.2f right %.2f gt4 %.2f\n",
                        static_cast<unsigned long long>(imagesSeen_), static_cast<unsigned long long>(stats_.runs), c,
                        voices[c], db(mixed[c][0]), db(mixed[c][1]), db(gt4[c]));
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
