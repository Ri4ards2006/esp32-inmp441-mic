/**
 * @file spectrum.cpp
 */
#include "spectrum.h"

// Include only the FFT + window headers: the umbrella <esp_dsp.h> pulls in mat.h,
// which includes <iostream> and drags ~240 KB of libstdc++ locale code into flash.
#include <dsps_fft2r.h>
#include <dsps_wind_hann.h>
#include "config.h"

namespace pipeline {

static_assert(Spectrum::kN == Config::FRAME_SAMPLES, "FFT size must equal frame size");

bool Spectrum::begin() {
    // nullptr: esp-dsp uses its internal static twiddle table (max size
    // CONFIG_DSP_MAX_FFT_SIZE, 4096 by default).
    if (dsps_fft2r_init_fc32(nullptr, kN) != ESP_OK) return false;
    dsps_wind_hann_f32(window_, kN);
    return true;
}

void Spectrum::compute(const float* x) {
    for (size_t i = 0; i < kN; ++i) {
        cplx_[2 * i] = x[i] * window_[i];
        cplx_[2 * i + 1] = 0.0f;
    }
    dsps_fft2r_fc32(cplx_, kN);
    dsps_bit_rev_fc32(cplx_, kN);
    for (size_t k = 0; k < kBins; ++k) {
        const float re = cplx_[2 * k];
        const float im = cplx_[2 * k + 1];
        power_[k] = re * re + im * im;
    }
}

}  // namespace pipeline
