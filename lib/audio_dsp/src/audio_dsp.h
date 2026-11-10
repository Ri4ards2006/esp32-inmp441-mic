/**
 * @file audio_dsp.h
 * @brief Level metering, spectral peak picking and energy VAD (pure C++, host-testable).
 *
 * dBFS convention
 * ---------------
 * Samples are normalised so digital full scale = 1.0 (see sample_format.h).
 * `amplitude_to_dbfs(a) = 20*log10(a)`, applied to RMS or peak values directly.
 * Consequence: a full-scale square wave reads 0 dBFS RMS, a full-scale *sine* reads
 * -3.01 dBFS RMS. Some datasheets (AES17 style) reference RMS to a full-scale sine
 * instead (sine = 0 dBFS); add +3.01 dB to compare against those.
 *
 * The FFT itself runs on the device via esp-dsp (src/pipeline/spectrum.cpp); this file
 * only contains the platform-independent post-processing so it can be unit-tested.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace mic {

/// Lower clamp for dB values; also returned for silence.
constexpr float kDbfsFloor = -120.0f;

float rms(const float* x, size_t n);
float peak_abs(const float* x, size_t n);
float amplitude_to_dbfs(float amplitude);

/**
 * Frequency of the strongest bin in a one-sided power spectrum, refined by parabolic
 * interpolation on the log-power of the peak and its two neighbours (accurate to a
 * small fraction of a bin for a Hann window).
 *
 * @param power    power spectrum, bins 0..bins-1 (e.g. N/2+1 bins of an N-point FFT)
 * @param bins     number of bins
 * @param bin_hz   bin spacing = fs / N
 * @param min_bin  bins below this are ignored (DC / rumble)
 * @return frequency in Hz, or 0 if the spectrum is all zero
 */
float spectral_peak_hz(const float* power, size_t bins, float bin_hz, size_t min_bin);

/**
 * Energy-based voice activity detector with an adaptive noise floor.
 *
 * Per frame it receives the frame level in dBFS:
 *  - noise floor: falls immediately to any quieter frame, rises slowly
 *    (floor_rise_db_per_frame) otherwise -> it tracks the running minimum, so a
 *    gradual change in ambient noise is followed without latching "voice".
 *  - active if level > floor + threshold_db AND level > min_level_dbfs.
 *  - hangover: stays active for hangover_frames after the last active frame so
 *    short pauses inside words do not chop the decision.
 *
 * Defaults are starting points, not tuned on hardware -- tune with docs/measurement.md.
 */
class EnergyVad {
public:
    struct Params {
        float    threshold_db            = 10.0f;
        float    min_level_dbfs          = -75.0f;
        uint32_t hangover_frames         = 8;
        float    floor_rise_db_per_frame = 0.05f;
    };

    EnergyVad() : EnergyVad(Params{}) {}
    explicit EnergyVad(const Params& p) : p_(p) {}

    /// Feed one frame level; returns the VAD decision for that frame.
    bool update(float level_dbfs);

    float noise_floor_dbfs() const { return floor_; }
    bool  active() const { return hang_ > 0; }

private:
    Params   p_;
    float    floor_ = 0.0f;
    bool     init_ = false;
    uint32_t hang_ = 0;
};

}  // namespace mic
