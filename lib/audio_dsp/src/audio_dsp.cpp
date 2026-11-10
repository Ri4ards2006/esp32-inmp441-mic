/**
 * @file audio_dsp.cpp
 */
#include "audio_dsp.h"

#include <cmath>

namespace mic {

float rms(const float* x, size_t n) {
    if (x == nullptr || n == 0) return 0.0f;
    // float accumulation is sufficient for n <= a few thousand normalised samples
    // and avoids the slow software double path on the ESP32 FPU.
    float acc = 0.0f;
    for (size_t i = 0; i < n; ++i) acc += x[i] * x[i];
    return std::sqrt(acc / static_cast<float>(n));
}

float peak_abs(const float* x, size_t n) {
    if (x == nullptr || n == 0) return 0.0f;
    float m = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float a = std::fabs(x[i]);
        if (a > m) m = a;
    }
    return m;
}

float amplitude_to_dbfs(float amplitude) {
    if (!(amplitude > 0.0f)) return kDbfsFloor;
    const float db = 20.0f * std::log10(amplitude);
    return db < kDbfsFloor ? kDbfsFloor : db;
}

float spectral_peak_hz(const float* power, size_t bins, float bin_hz, size_t min_bin) {
    if (power == nullptr || bins == 0 || min_bin >= bins) return 0.0f;
    size_t k = min_bin;
    for (size_t i = min_bin + 1; i < bins; ++i) {
        if (power[i] > power[k]) k = i;
    }
    if (!(power[k] > 0.0f)) return 0.0f;

    float delta = 0.0f;
    if (k > min_bin && k + 1 < bins) {
        constexpr float kTiny = 1e-30f;
        const float a = std::log10(power[k - 1] + kTiny);
        const float b = std::log10(power[k] + kTiny);
        const float c = std::log10(power[k + 1] + kTiny);
        const float den = a - 2.0f * b + c;
        if (den < 0.0f) delta = 0.5f * (a - c) / den;  // den < 0 at a true maximum
        if (delta > 0.5f) delta = 0.5f;
        if (delta < -0.5f) delta = -0.5f;
    }
    return (static_cast<float>(k) + delta) * bin_hz;
}

bool EnergyVad::update(float level_dbfs) {
    if (!init_) {
        floor_ = level_dbfs;
        init_ = true;
    } else if (level_dbfs < floor_) {
        floor_ = level_dbfs;  // fast fall
    } else {
        floor_ += p_.floor_rise_db_per_frame;  // slow rise, never above the level
        if (floor_ > level_dbfs) floor_ = level_dbfs;
    }

    const bool trigger =
        level_dbfs > floor_ + p_.threshold_db && level_dbfs > p_.min_level_dbfs;
    if (trigger) {
        hang_ = p_.hangover_frames + 1;  // this frame + hangover
    } else if (hang_ > 0) {
        --hang_;
    }
    return hang_ > 0;
}

}  // namespace mic
