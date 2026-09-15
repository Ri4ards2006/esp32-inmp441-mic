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
constexpr gpio_num_t PIN_I2S_SCK = GPIO_NUM_14;  ///< Serial Clock (BCLK)
constexpr gpio_num_t PIN_I2S_WS  = GPIO_NUM_15;  ///< Word Select / Frame Clock (LRCLK)
constexpr gpio_num_t PIN_I2S_SD  = GPIO_NUM_32;  ///< Serial Data In (SD)

// =============================================================================
// I2S Peripheral & DMA Configuration
// =============================================================================
constexpr i2s_port_t I2S_PORT            = I2S_NUM_0;
constexpr uint32_t   AUDIO_SAMPLE_RATE   = 15360;                     ///< 15.36 kHz hardware rate (SCK = 983 kHz, within INMP441 600k-3.3M spec)
constexpr uint32_t   DECIMATION_FACTOR   = 4;                         ///< Decimation factor: 15360 / 4 = 3840 Hz
constexpr uint32_t   EFFECTIVE_FREQ_HZ   = AUDIO_SAMPLE_RATE / DECIMATION_FACTOR; ///< 3840 Hz target for Edge Impulse
constexpr size_t     AUDIO_DMA_BUF_COUNT = 4;                         ///< Number of DMA ring buffers
constexpr size_t     AUDIO_DMA_BUF_LEN   = 256;                       ///< Samples per DMA buffer
constexpr size_t     AUDIO_CHUNK_SAMPLES = 256;                       ///< Samples processed per cycle

// =============================================================================
// FreeRTOS Task Parameters
// =============================================================================
constexpr uint32_t    AUDIO_TASK_STACK_SIZE = 4096;
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
constexpr bool STREAM_RAW_SAMPLES = true;

} // namespace Config
