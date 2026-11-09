// platform/audio/spatial (PLAN.md D9 step 3): the FFT against a plain DFT; the HRTF renderer's overlap-add against
// direct convolution of the same buses, within float rounding, over blocks of several sizes (the tail carried across
// them, blocks shorter than the tail too); an impulse; each azimuth's pair power-normalised.
#include "spatial/fft.h"
#include "spatial/hrtf.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace gt4::spatial;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

uint32_t seed = 12345;
float noise() {   // -1..1, fixed
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<int32_t>(seed) / 2147483648.0);
}

}  // namespace

int main() {
    // the FFT against a DFT, forward and back
    for (int n : {8, 64, 1024}) {
        std::vector<std::complex<float>> x(n), y;
        for (auto& v : x) v = {noise(), noise()};
        y = x;
        Fft(n).run(y.data(), false);
        double worst = 0;
        for (int k = 0; k < n; ++k) {
            std::complex<double> s;
            for (int t = 0; t < n; ++t)
                s += std::complex<double>(x[t]) * std::polar(1.0, -2.0 * 3.14159265358979323846 * k * t / n);
            worst = std::max(worst, std::abs(s - std::complex<double>(y[k])));
        }
        expect(worst < 1e-4 * n, "the FFT matches a DFT");
        Fft(n).run(y.data(), true);
        double back = 0;
        for (int t = 0; t < n; ++t) back = std::max(back, double(std::abs(y[t] - x[t])));
        expect(back < 1e-5, "the inverse FFT gives the input back");
    }

    HrtfRenderer hrtf;
    // the pairs: each ear's share and energy, the shares summing to 1
    bool normalised = true;
    for (int b = 0; b < kHrtfBuses; ++b) {
        double energy = 0;
        for (int e = 0; e < 2; ++e)
            for (int t = 0; t < kHrtfLength; ++t) energy += double(hrtf.hrir(b, e)[t]) * hrtf.hrir(b, e)[t];
        normalised = normalised && std::fabs(energy - 1.0) < 1e-4 &&
                     std::fabs(hrtf.share(b, 0) + hrtf.share(b, 1) - 1.0f) < 1e-5f;
    }
    expect(normalised, "every azimuth's pair has its two ears' energy summing to 1");
    expect(hrtf.share(18, 1) > 0.8f && hrtf.share(54, 0) > 0.8f, "90 degrees is on the right, 270 on the left");
    // every pair's first arrival 2 taps in (10 % of its peak), whatever its azimuth: the timing stereo has
    bool aligned = true;
    for (int b = 0; b < kHrtfBuses; ++b) {
        float peak = 0;
        for (int e = 0; e < 2; ++e)
            for (int t = 0; t < kHrtfLength; ++t) peak = std::max(peak, std::fabs(hrtf.hrir(b, e)[t]));
        int onset = kHrtfLength;
        for (int e = 0; e < 2; ++e) {
            int t = 0;
            while (t < kHrtfLength && std::fabs(hrtf.hrir(b, e)[t]) * 10 < peak) ++t;
            onset = std::min(onset, t);
        }
        aligned = aligned && onset == 2 && (b == 0 || std::abs(hrtf.lead(b) - hrtf.lead(b - 1)) <= 1);
    }
    expect(aligned, "every pair arrives 2 samples in; neighbouring leads differ by a tap at most");

    // overlap-add against direct convolution: three buses over blocks of several sizes
    const int sizes[] = {800, 801, 64, 1, 100, 2000, 800, 37, 800};
    int total = 0;
    for (int n : sizes) total += n;
    const int used[3] = {0, 18, 41};
    std::vector<float> input[3];
    for (auto& in : input) {
        in.resize(total);
        for (auto& v : in) v = std::round(noise() * 8000.0f);
    }
    std::vector<int32_t> out(2 * static_cast<size_t>(total), 0);
    int at = 0;
    for (int n : sizes) {
        std::vector<int32_t> buses(static_cast<size_t>(kHrtfBuses) * n, 0);
        std::vector<uint8_t> active(kHrtfBuses, 0);
        for (int u = 0; u < 3; ++u) {
            active[used[u]] = 1;
            for (int i = 0; i < n; ++i)
                buses[static_cast<size_t>(used[u]) * n + i] = static_cast<int32_t>(input[u][at + i]);
        }
        hrtf.render(buses.data(), active.data(), n, out.data() + 2 * at);
        at += n;
    }
    double worst = 0;
    for (int e = 0; e < 2; ++e) {
        std::vector<double> ref(total, 0.0);
        std::vector<float> part(total);
        for (int u = 0; u < 3; ++u) {
            convolveDirect(input[u].data(), total, hrtf.hrir(used[u], e), kHrtfLength, part.data());
            for (int i = 0; i < total; ++i) ref[i] += part[i];
        }
        for (int i = 0; i < total; ++i) worst = std::max(worst, std::fabs(out[2 * i + e] - ref[i]));
    }
    std::printf("hrtf: overlap-add against direct convolution over %d samples: at most %.3f apart\n", total, worst);
    expect(worst <= 1.0, "overlap-add equals direct convolution within rounding, whatever the block sizes");

    // an impulse on the front bus: its HRIR, from the impulse's own sample (no delay added)
    {
        HrtfRenderer h;
        const int n = 800;
        std::vector<int32_t> buses(static_cast<size_t>(kHrtfBuses) * n, 0), stereo(2 * n, 0);
        std::vector<uint8_t> active(kHrtfBuses, 0);
        active[0] = 1;
        buses[0] = 30000;
        h.render(buses.data(), active.data(), n, stereo.data());
        bool same = true;
        for (int t = 0; t < kHrtfLength; ++t)
            for (int e = 0; e < 2; ++e)
                same = same && std::abs(stereo[2 * t + e] - 30000.0 * h.hrir(0, e)[t]) <= 1.0;
        expect(same, "an impulse gives the HRIR, from its own sample");
        // the next block, with no bus active, plays nothing more (the tail was all in this one)
        std::vector<int32_t> next(2 * n, 0);
        std::fill(buses.begin(), buses.end(), 0);
        active[0] = 0;
        h.render(buses.data(), active.data(), n, next.data());
        bool silent = true;
        for (int32_t v : next) silent = silent && v == 0;
        expect(silent, "after the tail, silence");
    }

    if (failures) return 1;
    std::printf("hrtf: all checks passed\n");
    return 0;
}
