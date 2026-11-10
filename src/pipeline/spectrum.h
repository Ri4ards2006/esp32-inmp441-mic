/**
 * @file spectrum.h
 * @brief Hann-windowed real FFT power spectrum via esp-dsp (radix-2, float, AE32-optimised).
 */
#pragma once

#include <cstddef>

namespace pipeline {

class Spectrum {
public:
    static constexpr size_t kN = 512;            ///< FFT size == Config::FRAME_SAMPLES
    static constexpr size_t kBins = kN / 2 + 1;  ///< one-sided bins incl. DC and Nyquist

    /// Initialise twiddle tables and the window. Call once before compute().
    bool begin();

    /// Window x[0..kN-1] and fill power() with |X[k]|^2 for k = 0..kN/2.
    void compute(const float* x);

    const float* power() const { return power_; }

private:
    alignas(16) float window_[kN];
    alignas(16) float cplx_[2 * kN];  // interleaved re/im, in-place FFT buffer
    float power_[kBins];
};

}  // namespace pipeline
