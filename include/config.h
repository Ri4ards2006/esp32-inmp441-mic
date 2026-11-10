/**
 * @file config.h
 * @brief System configuration, pinouts, and hardware parameters for ESP32 + INMP441.
 */

#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

namespace Config {

// =============================================================================
// Hardware Pin Mappings (ESP32-WROOM-32D <-> INMP441)
// =============================================================================
constexpr gpio_num_t PIN_I2S_SCK    = GPIO_NUM_14;  ///< Serial Clock (BCLK)
constexpr gpio_num_t PIN_I2S_WS     = GPIO_NUM_15;  ///< Word Select / Frame Clock (LRCLK)
constexpr gpio_num_t PIN_I2S_SD     = GPIO_NUM_32;  ///< Serial Data In (SD)
constexpr gpio_num_t PIN_STATUS_LED = GPIO_NUM_2;   ///< Onboard Status LED (active HIGH)

// =============================================================================
// I2S Peripheral & DMA Configuration
// =============================================================================
constexpr i2s_port_t I2S_PORT            = I2S_NUM_0;
#ifndef MIC_SAMPLE_RATE
#define MIC_SAMPLE_RATE 15360
#endif
constexpr uint32_t   AUDIO_SAMPLE_RATE   = MIC_SAMPLE_RATE;           ///< I2S fs; BCLK = fs * 64 (override with -DMIC_SAMPLE_RATE=...)
constexpr uint32_t   DECIMATION_FACTOR   = 4;                         ///< Decimation factor: 15360 / 4 = 3840 Hz
constexpr uint32_t   EFFECTIVE_FREQ_HZ   = AUDIO_SAMPLE_RATE / DECIMATION_FACTOR; ///< 3840 Hz target for Edge Impulse
constexpr size_t     AUDIO_DMA_BUF_COUNT = 8;                         ///< Expanded DMA ring buffers to prevent overrun during inference
constexpr size_t     AUDIO_DMA_BUF_LEN   = 256;                       ///< Samples per DMA buffer
constexpr size_t     AUDIO_CHUNK_SAMPLES = 256;                       ///< Samples processed per cycle

/// Which 32-bit slot of each stereo frame carries the mic. 0 = first word of the pair
/// (left slot, L/R strapped to GND, as wired in this repo), 1 = second (L/R to VDD).
#ifndef MIC_I2S_SLOT
#define MIC_I2S_SLOT 0
#endif
constexpr size_t     MIC_SLOT_INDEX      = MIC_I2S_SLOT;
static_assert(MIC_SLOT_INDEX <= 1, "MIC_SLOT_INDEX must be 0 or 1");

/// -3 dB cutoff of the one-pole DC blocker applied after unpacking.
constexpr float      DC_BLOCK_CUTOFF_HZ  = 20.0f;

// =============================================================================
// Pipeline (env:esp32dev) -- capture -> process -> transport
// =============================================================================
constexpr size_t      FRAME_SAMPLES          = 512;   ///< Samples per frame (= FFT size). 32 ms @ 16 kHz
constexpr size_t      FRAME_POOL_SIZE        = 8;     ///< Frames in flight (power of two). 256 ms @ 16 kHz
constexpr uint32_t    I2S_READ_TIMEOUT_MS    = 100;   ///< Fail fast if BCLK/DMA stalls instead of hanging
constexpr uint32_t    STATS_INTERVAL_MS      = 1000;  ///< Serial telemetry period

// On-device DSP (process stage). VAD values are untuned starting points.
constexpr float       PEAK_MIN_HZ            = 60.0f;  ///< Ignore FFT bins below this (DC / rumble)
constexpr float       VAD_THRESHOLD_DB       = 10.0f;  ///< Margin over adaptive noise floor
constexpr float       VAD_MIN_LEVEL_DBFS     = -75.0f; ///< Absolute minimum frame level
constexpr uint32_t    VAD_HANGOVER_FRAMES    = 8;      ///< 8 x 32 ms = 256 ms
constexpr float       VAD_FLOOR_RISE_DB      = 0.05f;  ///< Per frame -> ~1.6 dB/s @ 16 kHz/512

constexpr uint32_t    CAPTURE_TASK_STACK     = 4096;
constexpr UBaseType_t CAPTURE_TASK_PRIORITY  = 6;     ///< Highest: must drain DMA in time
constexpr BaseType_t  CAPTURE_TASK_CORE      = 1;
constexpr uint32_t    PROCESS_TASK_STACK     = 8192;
constexpr UBaseType_t PROCESS_TASK_PRIORITY  = 5;
constexpr BaseType_t  PROCESS_TASK_CORE      = 1;
constexpr uint32_t    TRANSPORT_TASK_STACK   = 6144;
constexpr UBaseType_t TRANSPORT_TASK_PRIORITY = 3;
constexpr BaseType_t  TRANSPORT_TASK_CORE    = 0;     ///< Same core as the WiFi stack

// =============================================================================
// TinyML Inference & Detection Parameters
// =============================================================================
constexpr float    KEYWORD_CONFIDENCE_THRESHOLD = 0.65f;              ///< Minimum confidence for "hello" detection
constexpr uint32_t LED_ACTIVE_DURATION_MS       = 600;                ///< Status LED activation duration (ms)
constexpr size_t   INFERENCE_BUFFER_SIZE        = 3840;               ///< EI_CLASSIFIER_RAW_SAMPLE_COUNT (1000 ms at 3840 Hz)
constexpr size_t   INFERENCE_SLIDE_SAMPLES      = 960;                ///< 250 ms hop (4 inferences per second)
constexpr bool     ENABLE_ENERGY_THROTTLING     = true;               ///< Throttle NN inference when audio is below noise floor
constexpr int16_t  AUDIO_ACTIVITY_THRESHOLD     = 150;                ///< Minimum peak amplitude to trigger NN inference (~-45 dBFS)


// =============================================================================
// FreeRTOS Task Parameters
// =============================================================================
constexpr uint32_t    AUDIO_TASK_STACK_SIZE = 8192;                   ///< 8KB stack for audio acquisition & inference
constexpr UBaseType_t AUDIO_TASK_PRIORITY   = 5;                      ///< High priority for real-time audio
constexpr BaseType_t  AUDIO_TASK_CORE       = 1;                      ///< Pin audio acquisition to Core 1

// =============================================================================
// Diagnostics, Telemetry & Streaming Modes
// =============================================================================
constexpr uint32_t SERIAL_BAUD_RATE      = 115200;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 80;                        ///< Serial VU refresh rate (~12.5 fps)
constexpr size_t   VU_METER_WIDTH        = 30;                        ///< Width of ASCII VU meter bar

/**
 * When true, suppresses all textual banners and logs, streaming ONLY raw
 * 16-bit PCM integer samples (CSV: one value per line) for Edge Impulse.
 * When false, outputs human-readable real-time VU meter & metrics telemetry.
 */
constexpr bool STREAM_RAW_SAMPLES = false;


} // namespace Config
