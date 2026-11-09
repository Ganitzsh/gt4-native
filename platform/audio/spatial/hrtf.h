#pragma once
// D9's audio.spatial = hrtf (PLAN.md D9): 72 mono buses, one per azimuth of MIT KEMAR's horizontal plane
// (hrtf_kemar.h, 5 degrees apart, clockwise from ahead), each convolved with its pair of HRIRs and added to the stereo
// mix. The host's mixer routes each car's tagged voices to the two buses around its direction (platform/audio
// host_mixer).
// - Each azimuth's pair is power-normalised: its two ears' energy sums to 1, so a voice keeps the level GT4 gave it
//   (its distance gain), and the HRTF adds only the ears' differences and its colour.
// - Each pair is moved earlier by its own lead: two taps short of its earlier ear's onset (10 % of the pair's peak),
//   both ears alike, the end zero-filled. The interaural delay stays; what goes is the sound's way from the head's
//   centre to the nearer ear (from 1 tap at the side to 13 behind), so a voice's first arrival is 2 samples after
//   it at every azimuth, as close to audio.spatial = stereo's timing as an ear's response allows. At most 0.01 % of
//   an HRIR's energy lies before its lead, and neighbouring azimuths' leads differ by at most a tap.
// - Overlap-add on the mixer's own block, the FFT the next power of two at or above block + kHrtfLength - 1: nothing
//   is held back beyond the block D5 already mixes (no added latency); the convolution's tail goes into the next
//   block.
#include "fft.h"

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace gt4::spatial {

constexpr int kHrtfBuses = 72, kHrtfLength = 140;

class HrtfRenderer {
public:
    HrtfRenderer();

    // A bus's normalised HRIR for an ear (0 left, 1 right), kHrtfLength values; and the ear's share of the pair's
    // energy (the two sum to 1).
    const float* hrir(int bus, int ear) const { return hrir_.data() + (bus * 2 + ear) * kHrtfLength; }
    float share(int bus, int ear) const { return share_[bus * 2 + ear]; }
    int lead(int bus) const { return lead_[bus]; }   // the taps the pair was moved earlier by

    // One block of n samples: buses + b * n is bus b's mono input; the buses with active[b] set are convolved and
    // added to `stereo` (2n interleaved values), with what earlier blocks left (their tail). Returns the buses
    // convolved.
    int render(const int32_t* buses, const uint8_t* active, int n, int32_t* stereo);
    // Drops the tail (a new stream).
    void reset();
    // Whether a tail is still playing out (a block with no active bus still adds it).
    bool ringing() const { return ringing_; }

private:
    std::vector<float> hrir_;                 // [bus][ear][kHrtfLength]
    std::vector<float> share_;                // [bus][ear]
    std::vector<int> lead_;                   // [bus]
    std::unique_ptr<Fft> fft_;                // for the current block size
    std::vector<std::complex<float>> spectra_;   // [bus][ear][fft size]
    std::vector<std::complex<float>> work_, sum_[2];
    std::vector<float> tail_[2];              // kHrtfLength - 1 samples an ear
    bool ringing_ = false;
    void prepare(int n);
};

// Direct convolution, the reference for render()'s test: out[i] = sum over k of h[k] * x[i - k], for i < count.
void convolveDirect(const float* x, int count, const float* h, int taps, float* out);

}  // namespace gt4::spatial
