/**
 * @file main.cpp
 * @brief Real-Time On-Device Keyword Spotting ("hello") using Edge Impulse & ESP32 + INMP441.
 */

#include <Arduino.h>
#include "config.h"
#include "audio_input.h"
#include "audio_processor.h"
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

static AudioInput audioInput;
static AudioProcessor audioProcessor;
static TaskHandle_t audioTaskHandle = nullptr;

// 1000 ms sliding audio inference buffer (3840 raw int16_t PCM samples @ 3840 Hz)
static int16_t inference_buffer[Config::INFERENCE_BUFFER_SIZE];
static size_t inference_buf_count = 0;
static uint32_t led_turn_off_time = 0;

/**
 * @brief Signal callback to convert 16-bit integer PCM into normalized float samples for Edge Impulse.
 */
static int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    return numpy::int16_to_float(&inference_buffer[offset], out_ptr, length);
}

/**
 * @brief Dedicated FreeRTOS task running on Core 1 for deterministic audio acquisition & inference.
 */
static void audioTask(void* parameter) {
    int32_t pcm24_chunk[Config::AUDIO_CHUNK_SAMPLES];

    while (true) {
        // Check non-blocking status LED auto-off timer
        if (led_turn_off_time > 0 && millis() >= led_turn_off_time) {
            digitalWrite(Config::PIN_STATUS_LED, LOW);
            led_turn_off_time = 0;
        }

        size_t samples_read = audioInput.read(pcm24_chunk, Config::AUDIO_CHUNK_SAMPLES, portMAX_DELAY);

        if (samples_read > 0) {
            if (Config::STREAM_RAW_SAMPLES) {
                // Stream 16-bit PCM integer samples (CSV: one value per line) for data forwarder
                for (size_t i = 0; i < samples_read; i += Config::DECIMATION_FACTOR) {
                    int32_t val = pcm24_chunk[i] >> 6;
                    if (val > 32767) val = 32767;
                    if (val < -32768) val = -32768;
                    Serial.println(static_cast<int16_t>(val));
                }
            } else {
                // Decimate 15,360 Hz hardware I2S down to 3,840 Hz (M = 4) and append to inference buffer
                for (size_t i = 0; i < samples_read; i += Config::DECIMATION_FACTOR) {
                    int32_t val = pcm24_chunk[i] >> 6;
                    if (val > 32767) val = 32767;
                    if (val < -32768) val = -32768;

                    if (inference_buf_count < Config::INFERENCE_BUFFER_SIZE) {
                        inference_buffer[inference_buf_count++] = static_cast<int16_t>(val);
                    }
                }

                // Execute inference when buffer contains a complete 1000 ms window
                if (inference_buf_count >= Config::INFERENCE_BUFFER_SIZE) {
                    bool voice_active = true;
                    if (Config::ENABLE_ENERGY_THROTTLING) {
                        int32_t peak_mag = 0;
                        for (size_t i = 0; i < Config::INFERENCE_BUFFER_SIZE; ++i) {
                            int16_t s = inference_buffer[i];
                            int32_t mag = (s < 0) ? -s : s;
                            if (mag > peak_mag) peak_mag = mag;
                        }
                        if (peak_mag < Config::AUDIO_ACTIVITY_THRESHOLD) {
                            voice_active = false;
                        }
                    }

                    if (voice_active) {
                        signal_t signal;
                        signal.total_length = Config::INFERENCE_BUFFER_SIZE;
                        signal.get_data = &raw_feature_get_data;

                        ei_impulse_result_t result = { 0 };
                        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

                        if (r == EI_IMPULSE_OK) {
                            float hello_confidence = 0.0f;

                            // Scan predictions for "hello" target keyword
                            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                                if (strcmp(result.classification[ix].label, "hello") == 0) {
                                    hello_confidence = result.classification[ix].value;
                                }
                            }

                            // Trigger onboard LED on GPIO 2 if detection exceeds confidence threshold
                            if (hello_confidence >= Config::KEYWORD_CONFIDENCE_THRESHOLD) {
                                digitalWrite(Config::PIN_STATUS_LED, HIGH);
                                led_turn_off_time = millis() + Config::LED_ACTIVE_DURATION_MS;

                                Serial.printf("\r\n=======================================================\r\n");
                                Serial.printf(">>> [KEYWORD DETECTED] 'hello' (%.2f%% confidence) <<<\r\n", 
                                              hello_confidence * 100.0f);
                                Serial.printf("=======================================================\r\n");
                            }

                            // Log real-time telemetry and timing breakdown
                            Serial.printf("Predictions (DSP: %d ms, NN: %d ms): ", 
                                          result.timing.dsp, result.timing.classification);
                            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                                Serial.printf("%s: %.3f  ", 
                                              result.classification[ix].label, 
                                              result.classification[ix].value);
                            }
#if EI_CLASSIFIER_HAS_ANOMALY == 1
                            Serial.printf("anomaly: %.3f", result.anomaly);
#endif
                            Serial.println();
                        } else {
                            Serial.printf("[ERROR] Classifier failed with code: %d\r\n", r);
                        }
                    } else {
                        // Periodic idle heartbeat showing audio acquisition is active
                        static uint32_t last_idle_log_ms = 0;
                        uint32_t now = millis();
                        if (now - last_idle_log_ms >= 3000) {
                            last_idle_log_ms = now;
                            Serial.println("[IDLE] Listening... (ambient audio below threshold, NN inference throttled)");
                        }
                    }

                    // Shift buffer by hop size (250 ms slide = 960 samples)
                    size_t remaining = Config::INFERENCE_BUFFER_SIZE - Config::INFERENCE_SLIDE_SAMPLES;
                    memmove(inference_buffer, &inference_buffer[Config::INFERENCE_SLIDE_SAMPLES], remaining * sizeof(int16_t));
                    inference_buf_count = remaining;
                }

            }
        }

        taskYIELD();
    }
}

void setup() {
    Serial.begin(Config::SERIAL_BAUD_RATE);
    delay(500); // Allow serial and hardware lines to settle

    // Configure onboard status LED on GPIO 2
    pinMode(Config::PIN_STATUS_LED, OUTPUT);
    digitalWrite(Config::PIN_STATUS_LED, LOW);

    if (!Config::STREAM_RAW_SAMPLES) {
        Serial.println("\r\n==================================================");
        Serial.println("   ESP32-WROOM-32D Edge Impulse Keyword Spotter   ");
        Serial.println("==================================================");
        Serial.printf("Model Project   : %s (ID: %d)\r\n", 
                      EI_CLASSIFIER_PROJECT_NAME, EI_CLASSIFIER_PROJECT_ID);
        Serial.printf("Target Keyword  : 'hello' (Threshold: %.2f)\r\n", 
                      Config::KEYWORD_CONFIDENCE_THRESHOLD);
        Serial.printf("Hardware I2S    : %u Hz (SCK=%d, WS=%d, SD=%d)\r\n", 
                      Config::AUDIO_SAMPLE_RATE, Config::PIN_I2S_SCK, 
                      Config::PIN_I2S_WS, Config::PIN_I2S_SD);
        Serial.printf("DSP Decimation  : 15360 / %u = %u Hz (Window: %d samples)\r\n", 
                      Config::DECIMATION_FACTOR, Config::EFFECTIVE_FREQ_HZ, 
                      Config::INFERENCE_BUFFER_SIZE);
        Serial.printf("Status LED      : GPIO %d (Active HIGH for %u ms)\r\n", 
                      Config::PIN_STATUS_LED, Config::LED_ACTIVE_DURATION_MS);
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

    // Launch FreeRTOS acquisition & inference task pinned to Core 1
    BaseType_t task_created = xTaskCreatePinnedToCore(
        audioTask,
        "AudioInference",
        Config::AUDIO_TASK_STACK_SIZE,
        nullptr,
        Config::AUDIO_TASK_PRIORITY,
        &audioTaskHandle,
        Config::AUDIO_TASK_CORE
    );

    if (!Config::STREAM_RAW_SAMPLES) {
        if (task_created != pdPASS) {
            Serial.println("[ERROR] Failed to spawn audio inference task!");
        } else {
            Serial.println("[OK] Audio inference task active on Core 1.");
            Serial.println("Listening for keyword 'hello'...\r\n");
        }
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
