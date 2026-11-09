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

int32_t q15(double g) { return static_cast<int32_t>(std::lround(std::clamp(g, 0.0, 32767.0))); }

// A direction's azimuth in degrees, 0 ahead, clockwise (to the right), 0..360 (x right, z ahead negative); 0 for none.
double azimuthDegrees(const float direction[3]) {
    const double x = direction[0], z = direction[2];
    if (!std::isfinite(x) || !std::isfinite(z) || (x == 0.0 && z == 0.0)) return 0.0;
    const double a = std::atan2(x, -z) * 57.295779513082321;
    return a < 0.0 ? a + 360.0 : a;
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
    if (const char* cap = std::getenv("GT4_HRTF_BUSES"); cap && *cap)
        busCap_ = std::clamp(std::atoi(cap), 1, spatial::kHrtfBuses);
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
            const int n = static_cast<int>(value);
            scratch_.assign(2 * static_cast<size_t>(value), 0);
            spu_.tick();
            spu_.interpolation = static_cast<spu::Interpolation>(interpolation_.load());
            active_.fill(0);
            spatialGains();
            // hrtf's buses: while it is chosen, and after, until the last convolution's tail has played out
            const bool hrtf = static_cast<Spatial>(spatial_.load()) == Spatial::kHrtf;
            const bool render = hrtf || (hrtf_ && hrtf_->ringing());
            if (render) {
                if (!hrtf_) hrtf_ = std::make_unique<spatial::HrtfRenderer>();
                // (only the buses a voice writes this block are cleared: the others are neither mixed nor read)
                buses_.resize(static_cast<size_t>(spatial::kHrtfBuses) * n);
                for (int b = 0; b < spatial::kHrtfBuses; ++b)
                    if (active_[b])
                        std::fill_n(buses_.begin() + static_cast<ptrdiff_t>(b) * n, n, 0);
                spu_.buses = buses_.data();
                spu_.busCount = spatial::kHrtfBuses;
            } else {
                spu_.buses = nullptr;
                spu_.busCount = 0;
            }
            const auto t0 = std::chrono::steady_clock::now();
            spu_.mix(scratch_.data(), n, dryOnly_);
            if (render) {
                const auto t1 = std::chrono::steady_clock::now();
                const int used = hrtf_->render(buses_.data(), active_.data(), n, scratch_.data());
                const double hus =
                    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count();
                ++stats_.hrtfFrames;
                stats_.hrtfBuses += used;
                stats_.hrtfBusesMax = std::max<uint64_t>(stats_.hrtfBusesMax, used);
                stats_.hrtfMicroseconds += hus;
                stats_.hrtfMicrosecondsMax = std::max(stats_.hrtfMicrosecondsMax, hus);
            }
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

void HostMixer::hrtfRoute(uint32_t hw, const spu::Voice& v, const Gt4Spu2Tag& t, bool fromBed, const Last& before) {
    const double l = static_cast<int16_t>(v.voll << 1), r = static_cast<int16_t>(v.volr << 1);
    const double power = std::sqrt(l * l + r * r);
    // the two buses around the azimuth (5 degrees apart: hrtf_kemar.h), equal power between them
    const double at = azimuthDegrees(t.direction) / (360.0 / spatial::kHrtfBuses);
    const double whole = std::floor(at), f = at - whole;
    const int b0 = static_cast<int>(whole) % spatial::kHrtfBuses, b1 = (b0 + 1) % spatial::kHrtfBuses;
    const uint8_t bus[2] = {static_cast<uint8_t>(b0 + 1), static_cast<uint8_t>(b1 + 1)};
    const int32_t gain[2] = {q15(power * std::cos(f * 1.5707963267948966)),
                             q15(power * std::sin(f * 1.5707963267948966))};
    // ramped from last frame's buses and gains (the same car's same sound group: a moving sound), else from these
    Route& prev = routes_[hw];
    const bool ramp = prev.valid && prev.entrant == t.entrant && prev.group == t.group;
    spu::Spu2::VoiceGain g;
    g.on = true;
    auto from = [&](uint8_t b, int32_t to) {
        if (!ramp) return to;
        for (int k = 0; k < 2; ++k)
            if (prev.bus[k] == b) return prev.gain[k];
        return int32_t(0);
    };
    for (int k = 0; k < 2; ++k) {
        g.bus[g.routes] = bus[k];
        g.from[g.routes] = from(bus[k], gain[k]);
        g.to[g.routes] = gain[k];
        ++g.routes;
    }
    if (ramp)
        for (int k = 0; k < 2; ++k)
            if (prev.bus[k] != bus[0] && prev.bus[k] != bus[1] && prev.gain[k]) {
                g.bus[g.routes] = prev.bus[k];
                g.from[g.routes] = prev.gain[k];
                g.to[g.routes] = 0;
                ++g.routes;
            }
    g.wetLeft = g.wetRight = q15(power * 0.70710678118654752);
    g.wetLeftFrom = ramp ? before.wetLeft : g.wetLeft;
    g.wetRightFrom = ramp ? before.wetRight : g.wetRight;
    if (fromBed) {
        // it was in the bed last block: its buses fade in from nothing while its registers' stereo fades out, and
        // its effect sends move from the registers' to the buses'
        for (int k = 0; k < g.routes; ++k) g.from[k] = 0;
        g.ramp = true;
        g.leftFrom = before.left;
        g.rightFrom = before.right;
        g.left = g.right = 0;
        g.wetLeftFrom = before.left;
        g.wetRightFrom = before.right;
    }
    gains_[hw] = g;
    prev = Route{true, t.entrant, t.group, {bus[0], bus[1]}, {gain[0], gain[1]}};
}

void HostMixer::hrtfToBed(uint32_t hw, const spu::Voice& v, const Last& before) {
    // on its buses last block, in the bed now: the buses fade out while its registers' stereo fades in
    const Route& prev = routes_[hw];
    spu::Spu2::VoiceGain g;
    g.on = true;
    for (int k = 0; k < 2; ++k)
        if (prev.bus[k]) {
            g.bus[g.routes] = prev.bus[k];
            g.from[g.routes] = prev.gain[k];
            g.to[g.routes] = 0;
            ++g.routes;
        }
    g.ramp = true;
    g.leftFrom = g.rightFrom = 0;
    g.left = spu::volume(v.voll);
    g.right = spu::volume(v.volr);
    g.wetLeftFrom = before.wetLeft;
    g.wetRightFrom = before.wetRight;
    g.wetLeft = g.left;
    g.wetRight = g.right;
    gains_[hw] = g;
}

void HostMixer::capBuses() {
    // the buses' energy this frame (the gains they ramp from and to), the loudest busCap_ kept
    double energy[spatial::kHrtfBuses] = {};
    int used = 0;
    for (const auto& g : gains_)
        if (g.on)
            for (int r = 0; r < g.routes; ++r)
                energy[g.bus[r] - 1] += double(g.to[r]) * g.to[r] + double(g.from[r]) * g.from[r];
    for (double e : energy) used += e > 0.0;
    if (used <= busCap_) return;
    int order[spatial::kHrtfBuses];
    for (int b = 0; b < spatial::kHrtfBuses; ++b) order[b] = b;
    std::sort(order, order + spatial::kHrtfBuses, [&](int a, int b) { return energy[a] > energy[b]; });
    bool kept[spatial::kHrtfBuses] = {};
    for (int k = 0; k < busCap_; ++k) kept[order[k]] = energy[order[k]] > 0.0;
    auto nearest = [&](int b) {
        for (int d = 1; d <= spatial::kHrtfBuses / 2; ++d)
            for (int s : {b + d, b - d}) {
                const int c = (s + spatial::kHrtfBuses) % spatial::kHrtfBuses;
                if (kept[c]) return c;
            }
        return b;
    };
    for (auto& g : gains_) {
        if (!g.on) continue;
        // a dropped route's power goes to a kept route of the voice, else to the nearest kept bus
        for (int r = 0; r < g.routes; ++r) {
            const int b = g.bus[r] - 1;
            if (kept[b]) continue;
            int target = -1;
            for (int q = 0; q < g.routes; ++q)
                if (q != r && kept[g.bus[q] - 1]) target = q;
            if (target < 0) {
                g.bus[r] = static_cast<uint8_t>(nearest(b) + 1);
                continue;
            }
            g.from[target] = q15(std::hypot(double(g.from[target]), double(g.from[r])));
            g.to[target] = q15(std::hypot(double(g.to[target]), double(g.to[r])));
            g.from[r] = g.to[r] = 0;
        }
    }
}

void HostMixer::spatialGains() {
    spu_.gains = nullptr;
    const Spatial mode = static_cast<Spatial>(spatial_.load());
    const bool stereo = mode == Spatial::kStereo, hrtf = mode == Spatial::kHrtf;
    if (!tagsFresh_) ++stats_.framesWithoutTags;   // an older game, or an image whose tags didn't come: all the bed
    if (hrtf && !hrtf_) hrtf_ = std::make_unique<spatial::HrtfRenderer>();
    bool any = false;
    // (a voice keyed on since the last block: spu2 stamps its key-on with the frame before this block's tick)
    const uint32_t keyedFrame = spu_.counters.frame - 1;
    for (uint32_t hw = 0; hw < GT4_SPU2_TAG_VOICES; ++hw) {
        gains_[hw].on = false;
        Last& last = last_[hw];
        const Last before = last;
        last = Last{};
        const bool routed = routes_[hw].valid;
        routes_[hw].valid = false;
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
                panned = (stereo || hrtf) && !(v.voll & 0x8000) && !(v.volr & 0x8000);   // (sweep mode: the bed)
            }
        }
        const bool keyed = spu_.counters.konFrame[hw / 24][hw % 24] == keyedFrame;
        if (hrtf) {
            // the voice's last block: on its buses (routed), in the bed (before.on, not panned), or new; a voice
            // keyed on since starts where it is now
            const bool fromBuses = routed && !keyed, fromBed = before.on && !before.panned && !keyed;
            if (!panned) {
                last = Last{true, false, spu::volume(v.voll), spu::volume(v.volr), spu::volume(v.voll),
                            spu::volume(v.volr)};
                if (!fromBuses) continue;   // the bed, as original mixes it
                routes_[hw].valid = true;   // (hrtfToBed reads last block's route)
                hrtfToBed(hw, v, before);
                routes_[hw].valid = false;
                any = true;
                continue;
            }
            routes_[hw].valid = fromBuses;   // (hrtfRoute ramps from last block's route; a new sound doesn't)
            hrtfRoute(hw, v, t, fromBed, before);
            const spu::Spu2::VoiceGain& g = gains_[hw];
            last = Last{true, true, 0, 0, g.wetLeft, g.wetRight};
            ++stats_.voicesPanned;
            any = true;
            continue;
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
    if (hrtf && any) {
        capBuses();
        for (const auto& g : gains_)
            if (g.on)
                for (int r = 0; r < g.routes; ++r)
                    if (g.from[r] || g.to[r]) active_[g.bus[r] - 1] = 1;
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
        const spu::Spu2::VoiceGain& g = gains_[hw];
        if (g.on && g.routes && hrtf_) {   // hrtf: each bus's gain, its ears' shares of the pair's energy
            for (int k = 0; k < g.routes; ++k) {
                const double to = g.to[k] / 32768.0;
                mixed[t.entrant][0] += e * e * to * to * hrtf_->share(g.bus[k] - 1, 0);
                mixed[t.entrant][1] += e * e * to * to * hrtf_->share(g.bus[k] - 1, 1);
            }
        } else {
            const double ml = g.on ? g.left / 32768.0 : l, mr = g.on ? g.right / 32768.0 : r;
            mixed[t.entrant][0] += e * e * ml * ml;
            mixed[t.entrant][1] += e * e * mr * mr;
        }
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
