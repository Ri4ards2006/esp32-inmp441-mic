/**
 * @file main.cpp
 * @brief Production Audio Pipeline for ESP32 + INMP441 with Edge Impulse Data Streaming.
 */

#include <Arduino.h>
#include "config.h"
#include "audio_input.h"
#include "audio_processor.h"

static AudioInput audioInput;
static AudioProcessor audioProcessor;
static TaskHandle_t audioTaskHandle = nullptr;

/**
 * @brief Dedicated FreeRTOS task running on Core 1 for deterministic audio ingestion.
 */
static void audioTask(void* parameter) {
    int32_t pcm24_buffer[Config::AUDIO_CHUNK_SAMPLES];
    uint32_t last_telemetry_ms = 0;

    while (true) {
        size_t samples_read = audioInput.read(pcm24_buffer, Config::AUDIO_CHUNK_SAMPLES, portMAX_DELAY);

        if (samples_read > 0) {
            if (Config::STREAM_RAW_SAMPLES) {
                // Stream 16-bit PCM integer samples (CSV: one value per line)
                // Subsample/decimate by DECIMATION_FACTOR (15360 / 4 = 3840 Hz)
                for (size_t i = 0; i < samples_read; i += Config::DECIMATION_FACTOR) {
                    // 24-bit signed sample shifted by 6 provides a clean 4x gain for voice detection
                    int32_t val = pcm24_buffer[i] >> 6;
                    if (val > 32767) val = 32767;
                    if (val < -32768) val = -32768;
                    Serial.println(static_cast<int16_t>(val));
                }
            } else {
                // Human-readable diagnostic & visual VU telemetry
                AudioMetrics metrics = audioProcessor.analyze(pcm24_buffer, samples_read);

                uint32_t now = millis();
                if (now - last_telemetry_ms >= Config::TELEMETRY_INTERVAL_MS) {
                    last_telemetry_ms = now;

                    float display_level = metrics.rms * 15.0f;
                    String vu = AudioProcessor::createVUMeter(display_level, Config::VU_METER_WIDTH);

                    Serial.printf("VU: %s | Peak: %6.2f dBFS | RMS: %5.3f | MaxMag: %7d\r\n",
                                  vu.c_str(),
                                  metrics.dbfs,
                                  metrics.rms,
                                  metrics.raw_peak);
                }
            }
        }

        taskYIELD();
    }
}

void setup() {
    Serial.begin(Config::SERIAL_BAUD_RATE);
    delay(500); // Allow hardware lines to settle

    if (!Config::STREAM_RAW_SAMPLES) {
        Serial.println("\r\n==================================================");
        Serial.println("   ESP32-WROOM-32D + INMP441 Audio Ingestion      ");
        Serial.println("==================================================");
        Serial.printf("Hardware Rate   : %u Hz\r\n", Config::AUDIO_SAMPLE_RATE);
        Serial.printf("Output Stream   : %u Hz (Decimation: %u)\r\n", 
                      Config::EFFECTIVE_FREQ_HZ, Config::DECIMATION_FACTOR);
        Serial.printf("Pinout          : SCK=%d, WS=%d, SD=%d\r\n", 
                      Config::PIN_I2S_SCK, Config::PIN_I2S_WS, Config::PIN_I2S_SD);
        Serial.println("--------------------------------------------------");
    }

    esp_err_t err = audioInput.begin();
    if (err != ESP_OK) {
        if (!Config::STREAM_RAW_SAMPLES) {
            Serial.printf("[ERROR] Failed to start I2S audio driver: 0x%x\r\n", err);
        }
        while (true) {
            delay(1000);
        }
    }

    if (!Config::STREAM_RAW_SAMPLES) {
        Serial.println("[OK] I2S Driver initialized successfully.");
    }

    // Launch FreeRTOS acquisition task pinned to Core 1
    BaseType_t task_created = xTaskCreatePinnedToCore(
        audioTask,
        "AudioAcquisition",
        Config::AUDIO_TASK_STACK_SIZE,
        nullptr,
        Config::AUDIO_TASK_PRIORITY,
        &audioTaskHandle,
        Config::AUDIO_TASK_CORE
    );

    if (!Config::STREAM_RAW_SAMPLES) {
        if (task_created != pdPASS) {
            Serial.println("[ERROR] Failed to spawn audio FreeRTOS task!");
        } else {
            Serial.println("[OK] Audio FreeRTOS task spawned on Core 1.");
            Serial.println("Streaming real-time VU telemetry to Serial Monitor...\r\n");
        }
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
