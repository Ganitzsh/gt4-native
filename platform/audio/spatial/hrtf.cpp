// D9's HRTF renderer (hrtf.h).
#include "hrtf.h"

#include "hrtf_kemar.h"

#include <algorithm>
#include <cmath>

namespace gt4::spatial {

static_assert(kHrtfBuses == kHrtfAzimuths && kHrtfLength == kHrtfTaps);

HrtfRenderer::HrtfRenderer() : hrir_(kHrtfBuses * 2 * kHrtfLength, 0.0f), share_(kHrtfBuses * 2), lead_(kHrtfBuses) {
    for (int b = 0; b < kHrtfBuses; ++b) {
        // the lead: two taps short of the earlier ear's onset (10 % of the pair's peak)
        int peak = 0;
        for (int e = 0; e < 2; ++e)
            for (int t = 0; t < kHrtfTaps; ++t) peak = std::max(peak, std::abs(int(kHrtf[b][e][t])));
        int onset = kHrtfTaps;
        for (int e = 0; e < 2; ++e) {
            int t = 0;
            while (t < kHrtfTaps && std::abs(int(kHrtf[b][e][t])) * 10 < peak) ++t;
            onset = std::min(onset, t);
        }
        const int lead = std::max(0, onset - 2);
        lead_[b] = lead;
        double energy[2] = {};
        for (int e = 0; e < 2; ++e)
            for (int t = lead; t < kHrtfTaps; ++t) {
                const double v = kHrtf[b][e][t] * double(kHrtfScale);
                energy[e] += v * v;
            }
        const double norm = 1.0 / std::sqrt(energy[0] + energy[1]);   // the pair's energy to 1
        for (int e = 0; e < 2; ++e) {
            share_[b * 2 + e] = static_cast<float>(energy[e] / (energy[0] + energy[1]));
            for (int t = lead; t < kHrtfTaps; ++t)
                hrir_[(b * 2 + e) * kHrtfLength + t - lead] =
                    static_cast<float>(kHrtf[b][e][t] * double(kHrtfScale) * norm);
        }
    }
    for (auto& t : tail_) t.assign(kHrtfLength - 1, 0.0f);
}

void HrtfRenderer::prepare(int n) {
    const int size = nextPowerOfTwo(n + kHrtfLength - 1);
    if (fft_ && fft_->size() == size) return;
    fft_ = std::make_unique<Fft>(size);
    spectra_.assign(static_cast<size_t>(kHrtfBuses) * 2 * size, {});
    std::vector<std::complex<float>> h(size);
    for (int b = 0; b < kHrtfBuses; ++b)
        for (int e = 0; e < 2; ++e) {
            std::fill(h.begin(), h.end(), std::complex<float>());
            for (int t = 0; t < kHrtfLength; ++t) h[t] = hrir(b, e)[t];
            fft_->run(h.data(), false);
            std::copy(h.begin(), h.end(), spectra_.begin() + (static_cast<size_t>(b) * 2 + e) * size);
        }
    work_.assign(size, {});
    for (auto& s : sum_) s.assign(size, {});
}

void HrtfRenderer::reset() {
    for (auto& t : tail_) std::fill(t.begin(), t.end(), 0.0f);
    ringing_ = false;
}

int HrtfRenderer::render(const int32_t* buses, const uint8_t* active, int n, int32_t* stereo) {
    if (n <= 0) return 0;
    int list[kHrtfBuses], count = 0;
    for (int b = 0; b < kHrtfBuses; ++b)
        if (active[b]) list[count++] = b;
    const int keep = kHrtfLength - 1;
    if (!count && !ringing_) return 0;
    prepare(n);
    const int size = fft_->size();
    for (auto& s : sum_) std::fill(s.begin(), s.end(), std::complex<float>());
    // two buses a forward FFT (one the real part, one the imaginary), separated by the spectrum's symmetry
    for (int p = 0; p < count; p += 2) {
        const int a = list[p], b = p + 1 < count ? list[p + 1] : -1;
        for (int i = 0; i < n; ++i)
            work_[i] = {static_cast<float>(buses[static_cast<size_t>(a) * n + i]),
                        b >= 0 ? static_cast<float>(buses[static_cast<size_t>(b) * n + i]) : 0.0f};
        std::fill(work_.begin() + n, work_.end(), std::complex<float>());
        fft_->run(work_.data(), false);
        const std::complex<float>* ha[2] = {&spectra_[(static_cast<size_t>(a) * 2) * size],
                                            &spectra_[(static_cast<size_t>(a) * 2 + 1) * size]};
        const std::complex<float>* hb[2] = {
            b >= 0 ? &spectra_[(static_cast<size_t>(b) * 2) * size] : nullptr,
            b >= 0 ? &spectra_[(static_cast<size_t>(b) * 2 + 1) * size] : nullptr};
        for (int k = 0; k < size; ++k) {
            const std::complex<float> z = work_[k], zc = std::conj(work_[(size - k) & (size - 1)]);
            const std::complex<float> xa = 0.5f * (z + zc);
            const std::complex<float> d = 0.5f * (z - zc);
            const std::complex<float> xb(d.imag(), -d.real());   // (z - zc) / 2i
            for (int e = 0; e < 2; ++e) {
                const std::complex<float> h = ha[e][k];
                std::complex<float> y(xa.real() * h.real() - xa.imag() * h.imag(),
                                      xa.real() * h.imag() + xa.imag() * h.real());
                if (b >= 0) {
                    const std::complex<float> g = hb[e][k];
                    y += std::complex<float>(xb.real() * g.real() - xb.imag() * g.imag(),
                                             xb.real() * g.imag() + xb.imag() * g.real());
                }
                sum_[e][k] += y;
            }
        }
    }
    // both ears in one inverse FFT: left the real part, right the imaginary (both signals are real)
    for (int k = 0; k < size; ++k)
        work_[k] = {sum_[0][k].real() - sum_[1][k].imag(), sum_[0][k].imag() + sum_[1][k].real()};
    fft_->run(work_.data(), true);
    // overlap-add: this block's first n samples plus the earlier tail out now; the rest is the next tail
    bool ringing = false;
    for (int e = 0; e < 2; ++e) {
        std::vector<float>& tail = tail_[e];
        for (int i = 0; i < n; ++i) {
            float y = e ? work_[i].imag() : work_[i].real();
            if (i < keep) y += tail[i];
            stereo[2 * i + e] += static_cast<int32_t>(std::lround(y));
        }
        for (int j = 0; j < keep; ++j) {
            float y = n + j < size ? (e ? work_[n + j].imag() : work_[n + j].real()) : 0.0f;
            if (n + j < keep) y += tail[n + j];
            tail[j] = y;
            ringing = ringing || std::fabs(y) >= 0.5f;
        }
    }
    ringing_ = ringing;
    return count;
}

void convolveDirect(const float* x, int count, const float* h, int taps, float* out) {
    for (int i = 0; i < count; ++i) {
        double s = 0.0;
        for (int k = 0; k < taps && k <= i; ++k) s += double(h[k]) * x[i - k];
        out[i] = static_cast<float>(s);
    }
}

}  // namespace gt4::spatial
