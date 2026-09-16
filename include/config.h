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
constexpr uint32_t   AUDIO_SAMPLE_RATE   = 15360;                     ///< 15.36 kHz hardware rate (SCK = 983 kHz, within INMP441 600k-3.3M spec)
constexpr uint32_t   DECIMATION_FACTOR   = 4;                         ///< Decimation factor: 15360 / 4 = 3840 Hz
constexpr uint32_t   EFFECTIVE_FREQ_HZ   = AUDIO_SAMPLE_RATE / DECIMATION_FACTOR; ///< 3840 Hz target for Edge Impulse
constexpr size_t     AUDIO_DMA_BUF_COUNT = 8;                         ///< Expanded DMA ring buffers to prevent overrun during inference
constexpr size_t     AUDIO_DMA_BUF_LEN   = 256;                       ///< Samples per DMA buffer
constexpr size_t     AUDIO_CHUNK_SAMPLES = 256;                       ///< Samples processed per cycle

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
