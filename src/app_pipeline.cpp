/**
 * @file app_pipeline.cpp
 * @brief Entry point of the default firmware (env:esp32dev): INMP441 audio pipeline.
 */
#include <Arduino.h>
#include "config.h"
#include "pipeline/pipeline.h"

void setup() {
    Serial.begin(Config::SERIAL_BAUD_RATE);
    delay(200);
    Serial.printf("\r\n[boot] INMP441 pipeline: fs=%u Hz, BCLK=%u Hz, frame=%u samples, "
                  "pins SCK=%d WS=%d SD=%d\r\n",
                  Config::AUDIO_SAMPLE_RATE, Config::AUDIO_SAMPLE_RATE * 64,
                  static_cast<unsigned>(Config::FRAME_SAMPLES), Config::PIN_I2S_SCK,
                  Config::PIN_I2S_WS, Config::PIN_I2S_SD);

    if (!pipeline::start()) {
        Serial.println("[boot] ERROR: pipeline start failed (I2S driver or task creation)");
        return;
    }
    Serial.println("[boot] pipeline running");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
