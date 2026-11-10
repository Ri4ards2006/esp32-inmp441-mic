/**
 * @file pipeline.h
 * @brief Three-stage FreeRTOS audio pipeline: capture -> process -> transport.
 *
 * Memory model
 * ------------
 * A fixed pool of FRAME_POOL_SIZE frames is allocated statically. Every frame is at
 * any time owned by exactly one place: the free ring, the capture task, the process
 * ring/task or the transport ring/task. Frames move by pointer through three
 * lock-free SPSC rings:
 *
 *     free_ring --(capture)--> proc_ring --(process)--> tx_ring --(transport)--> free_ring
 *
 * Each ring has capacity == pool size, so a push can never fail. The only place where
 * data can be lost is capture: if no free frame is available at a frame boundary, the
 * samples of that frame period are still drained from DMA (keeping the I2S DMA ring
 * from overflowing) but discarded, and `frames_dropped` is incremented. Back-pressure
 * from a slow network therefore never stalls the I2S DMA.
 *
 * Blocking
 * --------
 * - capture blocks only in i2s_read (waiting for the next DMA buffer), with a timeout.
 * - process / transport sleep on task notifications sent by their producer.
 * No stage ever waits on another stage's lock.
 */
#pragma once

#include <Arduino.h>
#include <atomic>
#include "config.h"

namespace pipeline {

/// Per-frame features computed by the process stage.
struct Features {
    float   rms_dbfs  = -120.0f;  ///< RMS level, dB re. full scale (see audio_dsp.h)
    float   peak_dbfs = -120.0f;  ///< Sample peak, dB re. full scale
    float   peak_hz   = 0.0f;     ///< Dominant frequency (FFT, interpolated)
    float   noise_floor_dbfs = -120.0f;  ///< VAD noise-floor estimate
    bool    voice     = false;    ///< Energy VAD decision
};

struct AudioFrame {
    uint32_t sample_index;                    ///< Index of samples[0] since boot
    float    samples[Config::FRAME_SAMPLES];  ///< DC-blocked, normalised [-1, 1)
    int16_t  pcm[Config::FRAME_SAMPLES];      ///< int16 PCM for transport
    Features features;
};

struct Stats {
    std::atomic<uint32_t> frames_captured{0};
    std::atomic<uint32_t> frames_dropped{0};   ///< No free frame at capture time
    std::atomic<uint32_t> frames_processed{0};
    std::atomic<uint32_t> frames_sent{0};
    std::atomic<uint32_t> send_errors{0};
    std::atomic<uint32_t> i2s_timeouts{0};
    std::atomic<uint32_t> process_us_max{0};   ///< Worst-case process time per frame
    std::atomic<uint32_t> process_us_last{0};
};

/// Starts all three tasks. Returns false if any allocation / task creation failed.
bool start();

const Stats& stats();

}  // namespace pipeline
