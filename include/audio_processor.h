/**
 * @file audio_processor.h
 * @brief Signal conditioning, metrics (RMS, Peak, dBFS), and VU visualizer.
 */

#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include "sample_format.h"

/**
 * @brief Container for real-time audio statistics.
 */
struct AudioMetrics {
    float   rms;             ///< Normalized RMS amplitude (0.0f to 1.0f)
    float   peak;            ///< Normalized peak amplitude (0.0f to 1.0f)
    float   dbfs;            ///< Decibels Full Scale (-96.0 dBFS to 0.0 dBFS)
    int32_t raw_peak;        ///< Raw 24-bit peak sample magnitude
};

/**
 * @brief Handles audio DSP calculations, scaling, and visualization.
 */
class AudioProcessor {
public:
    AudioProcessor();

    /**
     * @brief Analyzes a buffer of signed 24-bit samples (as returned by AudioInput::read,
     *        i.e. already unpacked from the 32-bit slot -- do NOT shift again).
     * @param raw_samples Array of signed 24-bit samples in int32_t.
     * @param sample_count Number of samples in the frame.
     * @return AudioMetrics containing calculated RMS, peak, and dBFS values.
     */
    AudioMetrics analyze(const int32_t* raw_samples, size_t sample_count);

    /**
     * @brief Generates an ASCII VU-meter bar.
     * @param level Normalized level between 0.0 and 1.0.
     * @param bar_width Total character width of the meter bar.
     * @return String representation of the VU meter (e.g., "[=========>           ]").
     */
    static String createVUMeter(float level, size_t bar_width = 25);

    /**
     * @brief Normalizes a raw 32-bit I2S slot into a float in range [-1.0f, 1.0f).
     */
    static inline float normalizeSample(int32_t raw_slot) {
        return mic::s24_to_float(mic::slot_to_s24(raw_slot));
    }
};

