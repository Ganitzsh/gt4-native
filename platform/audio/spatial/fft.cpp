// A radix-2 complex FFT (fft.h).
#include "fft.h"

#include <cmath>
#include <utility>

namespace gt4::spatial {

int nextPowerOfTwo(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

Fft::Fft(int n) : n_(n), reverse_(n), twiddle_(n / 2) {
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
        reverse_[i] = r;
    }
    for (int k = 0; k < n / 2; ++k) {
        const double a = -2.0 * 3.14159265358979323846 * k / n;
        twiddle_[k] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
    }
}

void Fft::run(std::complex<float>* a, bool inverse) const {
    const int n = n_;
    for (int i = 0; i < n; ++i)
        if (i < reverse_[i]) std::swap(a[i], a[reverse_[i]]);
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len / 2, stride = n / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < half; ++k) {
                // (the product by hand: std::complex's operator* checks for NaNs, slowly, without fast math)
                const std::complex<float> t = twiddle_[k * stride];
                const float wr = t.real(), wi = inverse ? -t.imag() : t.imag();
                const std::complex<float> x = a[i + k + half];
                const std::complex<float> v(x.real() * wr - x.imag() * wi, x.real() * wi + x.imag() * wr);
                const std::complex<float> u = a[i + k];
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
    }
    if (inverse) {
        const float scale = 1.0f / static_cast<float>(n);
        for (int i = 0; i < n; ++i) a[i] *= scale;
    }
}

}  // namespace gt4::spatial
