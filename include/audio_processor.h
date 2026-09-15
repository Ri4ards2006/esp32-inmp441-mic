/**
 * @file audio_processor.h
 * @brief Signal conditioning, metrics (RMS, Peak, dBFS), and VU visualizer.
 */

#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstddef>

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
     * @brief Analyzes a buffer of raw 32-bit I2S samples from INMP441.
     * @param raw_samples Array of 32-bit words from I2S DMA.
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
     * @brief Normalizes raw 32-bit I2S sample into a float in range [-1.0f, 1.0f].
     * Scales INMP441 24-bit MSB-justified data.
     */
    static inline float normalizeSample(int32_t raw) {
        // INMP441 places 24 active bits in the MSBs of the 32-bit word.
        int32_t sample24 = raw >> 8;
        return static_cast<float>(sample24) / 8388607.0f;
    }
};

