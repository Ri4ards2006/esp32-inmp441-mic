/**
 * @file sample_format.h
 * @brief INMP441 sample unpacking, scaling and DC removal (pure C++, host-testable).
 *
 * Why this file exists
 * --------------------
 * The INMP441 is a 24-bit I2S MEMS microphone. Per I2S frame (one WS period) the
 * ESP32 master clocks 64 SCK cycles: 32 for the left slot, 32 for the right slot.
 * The mic drives SD only in the slot selected by its L/R pin and tri-states in the
 * other one. Inside its slot it sends the sample MSB first, starting one SCK after
 * the WS edge (Philips I2S, hence I2S_COMM_FORMAT_STAND_I2S), 24 data bits followed
 * by 8 bits that are not part of the sample.
 *
 * With I2S_BITS_PER_SAMPLE_32BIT the ESP32 DMA stores each slot as a 32-bit word,
 * left-aligned, so the 24-bit two's-complement sample sits in bits [31:8]:
 *
 *     bit 31 ........................ 8 | 7 ...... 0
 *         s23 (sign) ............... s0 | don't care
 *
 * Unpacking therefore is an *arithmetic* right shift by 8: it drops the low byte and
 * keeps the sign. (GCC guarantees arithmetic shift for signed int; C++20 mandates it.)
 *
 * Scaling: we normalise by 2^23 so that the full code range [-2^23, 2^23-1] maps to
 * [-1.0, 1.0). Dividing by 2^23-1 (as the old code did) is asymmetric and lets the
 * most negative code fall below -1.0.
 *
 * DC offset: MEMS mics plus the digital path can carry a static offset. Any offset
 * biases RMS/dBFS, the VAD energy and shows up in FFT bin 0 leakage. A one-pole
 * DC blocker (first-order high-pass) removes it with 2 multiply-adds per sample:
 *
 *     y[n] = x[n] - x[n-1] + R * y[n-1],   R = exp(-2*pi*fc/fs)
 *
 * fc around 10-20 Hz is far below speech (>~80 Hz) and below the mic's own
 * low-frequency roll-off, so it does not colour the audio band.
 */
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mic {

/// Full-scale divisor for 24-bit two's complement (2^23).
constexpr float kS24FullScale = 8388608.0f;

/// Extract the signed 24-bit sample from a left-aligned 32-bit I2S slot.
inline int32_t slot_to_s24(int32_t slot) {
    return slot >> 8;  // arithmetic shift: discard bits [7:0], keep sign
}

/// Normalise a signed 24-bit sample to [-1.0, 1.0).
inline float s24_to_float(int32_t s24) {
    return static_cast<float>(s24) / kS24FullScale;
}

/// Convert a normalised float to int16 PCM with rounding and saturation.
inline int16_t float_to_s16(float x) {
    float v = x * 32768.0f;
    v = v >= 0.0f ? v + 0.5f : v - 0.5f;  // round half away from zero
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return static_cast<int16_t>(v);
}

/**
 * One-pole DC blocker. State persists between process() calls so it can run on a
 * continuous stream split into DMA-sized blocks.
 */
class DcBlocker {
public:
    explicit DcBlocker(float r = 0.995f) : r_(r) {}

    /// Pole radius for a -3 dB cutoff fc at sample rate fs.
    static float coefficient(float fc_hz, float fs_hz) {
        return std::exp(-2.0f * 3.14159265358979f * fc_hz / fs_hz);
    }

    /// In-place filtering of n samples.
    void process(float* buf, size_t n) {
        float x1 = x1_, y1 = y1_;
        for (size_t i = 0; i < n; ++i) {
            const float x = buf[i];
            const float y = x - x1 + r_ * y1;
            x1 = x;
            y1 = y;
            buf[i] = y;
        }
        x1_ = x1;
        y1_ = y1;
    }

    void reset() { x1_ = 0.0f; y1_ = 0.0f; }

private:
    float r_;
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

}  // namespace mic
