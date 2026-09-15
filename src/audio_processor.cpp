/**
 * @file audio_processor.cpp
 * @brief Implementation of audio DSP metrics and visualization.
 */

#include "audio_processor.h"
#include <cmath>
#include <algorithm>

AudioProcessor::AudioProcessor() {}

AudioMetrics AudioProcessor::analyze(const int32_t* raw_samples, size_t sample_count) {
    AudioMetrics metrics = {
        .rms = 0.0f,
        .peak = 0.0f,
        .dbfs = -96.0f,
        .raw_peak = 0
    };

    if (raw_samples == nullptr || sample_count == 0) {
        return metrics;
    }

    double sum_squares = 0.0;
    int32_t max_abs = 0;

    for (size_t i = 0; i < sample_count; ++i) {
        // Extract 24-bit signed sample from 32-bit container
        int32_t sample24 = raw_samples[i] >> 8;
        int32_t abs_val = std::abs(sample24);
        if (abs_val > max_abs) {
            max_abs = abs_val;
        }

        float normalized = static_cast<float>(sample24) / 8388607.0f;
        sum_squares += static_cast<double>(normalized * normalized);
    }

    metrics.raw_peak = max_abs;
    metrics.peak = static_cast<float>(max_abs) / 8388607.0f;

    float mean_square = static_cast<float>(sum_squares / sample_count);
    metrics.rms = std::sqrt(mean_square);

    // Compute dBFS (Decibels relative to Full Scale)
    if (metrics.rms > 1e-5f) {
        metrics.dbfs = 20.0f * std::log10(metrics.rms);
    } else {
        metrics.dbfs = -96.0f;
    }

    // Clamp dBFS to realistic dynamic range
    if (metrics.dbfs < -96.0f) {
        metrics.dbfs = -96.0f;
    } else if (metrics.dbfs > 0.0f) {
        metrics.dbfs = 0.0f;
    }

    return metrics;
}

String AudioProcessor::createVUMeter(float level, size_t bar_width) {
    if (bar_width < 5) {
        bar_width = 5;
    }

    // Clamp level to [0.0, 1.0]
    float clamped = std::max(0.0f, std::min(1.0f, level));
    size_t filled = static_cast<size_t>(std::round(clamped * static_cast<float>(bar_width)));

    String bar = "[";
    for (size_t i = 0; i < bar_width; ++i) {
        if (i < filled) {
            if (i == filled - 1 && filled < bar_width) {
                bar += ">";
            } else {
                bar += "=";
            }
        } else {
            bar += " ";
        }
    }
    bar += "]";
    return bar;
}

