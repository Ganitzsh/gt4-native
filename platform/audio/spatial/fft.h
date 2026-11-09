#pragma once
// A radix-2 complex FFT (iterative, in place, float), written for this project: the HRTF renderer's overlap-add
// convolution (hrtf.h, PLAN.md D9). No library.
#include <complex>
#include <vector>

namespace gt4::spatial {

// The next power of two at or above n (n >= 1).
int nextPowerOfTwo(int n);

// One size's FFT: its bit-reversal order and twiddles computed once (in double), the transforms in float.
class Fft {
public:
    explicit Fft(int n);   // n a power of two, at least 2
    int size() const { return n_; }
    // In place over size() values. inverse: the inverse transform, scaled by 1 / size().
    void run(std::complex<float>* data, bool inverse) const;

private:
    int n_;
    std::vector<int> reverse_;
    std::vector<std::complex<float>> twiddle_;   // e^(-2 pi i k / n), k < n / 2
};

}  // namespace gt4::spatial
