/**
 * @file main.cpp
 * @brief Application entry point for ESP32 + INMP441 I2S Audio Acquisition.
 */

#include <Arduino.h>
#include "config.h"
#include "audio_input.h"
#include "audio_processor.h"

// Singleton driver and processor instances
static AudioInput audioInput;
static AudioProcessor audioProcessor;

// FreeRTOS Task Handle
static TaskHandle_t audioTaskHandle = nullptr;

/**
 * @brief Dedicated FreeRTOS task for I2S audio stream ingestion and processing.
 * Pinned to Core 1 to guarantee deterministic, zero-jitter DMA buffer reads.
 */
static void audioTask(void* parameter) {
    int32_t raw_buffer[Config::AUDIO_CHUNK_SAMPLES];
    uint32_t last_telemetry_ms = 0;

    Serial.println("[Task] Audio acquisition task running on Core " + String(xPortGetCoreID()));

    while (true) {
        size_t samples_read = audioInput.read(raw_buffer, Config::AUDIO_CHUNK_SAMPLES, portMAX_DELAY);

        if (samples_read > 0) {
            AudioMetrics metrics = audioProcessor.analyze(raw_buffer, samples_read);

            uint32_t now = millis();
            if (now - last_telemetry_ms >= Config::TELEMETRY_INTERVAL_MS) {
                last_telemetry_ms = now;

                // Scale level for visualization (boost low sensitivity for clear display)
                float display_level = metrics.rms * 15.0f;
                String vu = AudioProcessor::createVUMeter(display_level, Config::VU_METER_WIDTH);

                // Formatted real-time telemetry line
                Serial.printf("VU: %s | Peak: %6.2f dBFS | RMS: %5.3f | MaxMag: %7d\r\n",
                              vu.c_str(),
                              metrics.dbfs,
                              metrics.rms,
                              metrics.raw_peak);
            }
        }

        // Yield execution to allow other equal priority tasks if any
        taskYIELD();
    }
}

void setup() {
    Serial.begin(Config::SERIAL_BAUD_RATE);
    delay(1000); // Allow UART and power rails to stabilize

    Serial.println("\r\n==================================================");
    Serial.println("   ESP32-WROOM-32D + INMP441 I2S Audio System     ");
    Serial.println("==================================================");
    Serial.printf("Sample Rate : %u Hz\r\n", Config::AUDIO_SAMPLE_RATE);
    Serial.printf("DMA Buffers : %u x %u samples\r\n", Config::AUDIO_DMA_BUF_COUNT, Config::AUDIO_DMA_BUF_LEN);
    Serial.printf("Pinout      : SCK=%d, WS=%d, SD=%d\r\n", 
                  Config::PIN_I2S_SCK, Config::PIN_I2S_WS, Config::PIN_I2S_SD);
    Serial.println("--------------------------------------------------");

    esp_err_t err = audioInput.begin();
    if (err != ESP_OK) {
        Serial.printf("[ERROR] Failed to start I2S audio driver: 0x%x\r\n", err);
        while (true) {
            delay(1000);
        }
    }
    Serial.println("[OK] I2S Driver initialized successfully.");

    // Launch real-time processing task pinned to Core 1
    BaseType_t task_created = xTaskCreatePinnedToCore(
        audioTask,
        "AudioAcquisition",
        Config::AUDIO_TASK_STACK_SIZE,
        nullptr,
        Config::AUDIO_TASK_PRIORITY,
        &audioTaskHandle,
        Config::AUDIO_TASK_CORE
    );

    if (task_created != pdPASS) {
        Serial.println("[ERROR] Failed to create audio acquisition FreeRTOS task!");
    } else {
        Serial.println("[OK] Audio FreeRTOS task spawned on Core 1.");
    }

    Serial.println("Streaming real-time VU telemetry to Serial Monitor...\r\n");
}

void loop() {
    // Arduino loop remains available for non-audio duties (WiFi, WebSockets, MQTT)
    vTaskDelay(pdMS_TO_TICKS(1000));
}

