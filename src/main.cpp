/**
 * @file main.cpp
 * @brief Isolated Hardware Diagnostic & Signal Verification for INMP441 I2S Microphone.
 * 
 * Hardware Pinout (ESP32-WROOM-32D <-> INMP441):
 *   SCK  -> GPIO 14 (Bit Clock / BCLK)
 *   WS   -> GPIO 15 (Word Select / LRCLK)
 *   SD   -> GPIO 32 (Serial Data Out from Mic)
 *   VDD  -> 3V3      (Operating power: 1.8V - 3.3V)
 *   GND  -> GND      (Common Ground)
 *   L/R  -> GND      (Channel select: GND = Left, VDD = Right)
 */

#include <Arduino.h>
#include <driver/i2s.h>
#include <cmath>
#include <algorithm>

// Pin Configuration
#define PIN_I2S_SCK GPIO_NUM_14
#define PIN_I2S_WS  GPIO_NUM_15
#define PIN_I2S_SD  GPIO_NUM_32
#define I2S_PORT    I2S_NUM_0

// Audio Sampling Parameters
#define SAMPLE_RATE         16000
#define SAMPLES_PER_CHANNEL 256
#define TOTAL_SAMPLES       (SAMPLES_PER_CHANNEL * 2) // Capture Stereo pairs [Left, Right]

static int32_t raw_buffer[TOTAL_SAMPLES];

static esp_err_t initI2SHardware() {
    const i2s_config_t i2s_config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        // Capture stereo to simultaneously test BOTH Left and Right I2S slots
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = static_cast<i2s_comm_format_t>(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = SAMPLES_PER_CHANNEL,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    const i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_SCK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_SD
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] i2s_driver_install failed: 0x%x\r\n", err);
        return err;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] i2s_set_pin failed: 0x%x\r\n", err);
        i2s_driver_uninstall(I2S_PORT);
        return err;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    return ESP_OK;
}

static String renderVUBars(float left_norm, float right_norm, size_t width = 18) {
    auto makeBar = [width](float level) {
        float clamped = std::max(0.0f, std::min(1.0f, level));
        size_t filled = static_cast<size_t>(clamped * width);
        String s = "[";
        for (size_t i = 0; i < width; ++i) {
            if (i < filled) {
                s += (i == filled - 1 && filled < width) ? ">" : "=";
            } else {
                s += " ";
            }
        }
        s += "]";
        return s;
    };
    return "L: " + makeBar(left_norm * 12.0f) + "  R: " + makeBar(right_norm * 12.0f);
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Allow serial and power to settle

    Serial.println("\r\n========================================================");
    Serial.println("     INMP441 I2S Hardware Audio Diagnostic Test Suite   ");
    Serial.println("========================================================");
    Serial.printf("Configured Pins : SCK=GPIO %d | WS=GPIO %d | SD=GPIO %d\r\n", 
                  PIN_I2S_SCK, PIN_I2S_WS, PIN_I2S_SD);
    Serial.printf("Sample Rate     : %u Hz (Stereo acquisition for slot test)\r\n", SAMPLE_RATE);
    Serial.println("Channel Mode    : Stereo Dual-Slot (Left=L/R tied GND, Right=L/R tied 3V3)");
    Serial.println("--------------------------------------------------------");

    esp_err_t err = initI2SHardware();
    if (err != ESP_OK) {
        Serial.println("[CRITICAL] Failed to initialize ESP32 I2S hardware peripheral.");
        while (true) {
            delay(1000);
        }
    }

    Serial.println("[OK] I2S Driver successfully installed and DMA listening.");
    Serial.println("Speak, tap, or blow on the microphone to test live audio response.\r\n");
}

void loop() {
    static uint32_t last_telemetry_ms = 0;
    static uint32_t last_report_ms = 0;
    static uint32_t total_blocks_read = 0;

    size_t bytes_read = 0;
    esp_err_t err = i2s_read(I2S_PORT, raw_buffer, sizeof(raw_buffer), &bytes_read, portMAX_DELAY);
    if (err != ESP_OK || bytes_read == 0) {
        return;
    }

    total_blocks_read++;
    size_t pairs_read = bytes_read / (sizeof(int32_t) * 2);

    int32_t left_min = 8388607, left_max = -8388608;
    int32_t right_min = 8388607, right_max = -8388608;
    uint32_t left_nonzeros = 0, right_nonzeros = 0;
    double left_sum_sq = 0.0, right_sum_sq = 0.0;

    int32_t sample_l_raw0 = raw_buffer[0];
    int32_t sample_r_raw0 = raw_buffer[1];

    for (size_t i = 0; i < pairs_read; ++i) {
        // INMP441 places 24-bit audio in MSBs of 32-bit word: extract signed 24-bit
        int32_t l_val = raw_buffer[2 * i] >> 8;
        int32_t r_val = raw_buffer[2 * i + 1] >> 8;

        if (l_val != 0) left_nonzeros++;
        if (r_val != 0) right_nonzeros++;

        if (l_val < left_min) left_min = l_val;
        if (l_val > left_max) left_max = l_val;

        if (r_val < right_min) right_min = r_val;
        if (r_val > right_max) right_max = r_val;

        float l_norm = static_cast<float>(l_val) / 8388607.0f;
        float r_norm = static_cast<float>(r_val) / 8388607.0f;
        left_sum_sq += static_cast<double>(l_norm * l_norm);
        right_sum_sq += static_cast<double>(r_norm * r_norm);
    }

    float left_rms = std::sqrt(static_cast<float>(left_sum_sq / pairs_read));
    float right_rms = std::sqrt(static_cast<float>(right_sum_sq / pairs_read));
    int32_t left_p2p = (left_nonzeros > 0) ? (left_max - left_min) : 0;
    int32_t right_p2p = (right_nonzeros > 0) ? (right_max - right_min) : 0;

    uint32_t now = millis();

    // High-frequency telemetry bar (every 100 ms)
    if (now - last_telemetry_ms >= 100) {
        last_telemetry_ms = now;
        String vu = renderVUBars(left_rms, right_rms);
        Serial.printf("%s | L_RMS: %5.3f P2P: %7d | R_RMS: %5.3f P2P: %7d\r\n",
                      vu.c_str(), left_rms, left_p2p, right_rms, right_p2p);
    }

    // Comprehensive diagnostic report (every 2.5 seconds)
    if (now - last_report_ms >= 2500) {
        last_report_ms = now;

        float left_pct = (static_cast<float>(left_nonzeros) / pairs_read) * 100.0f;
        float right_pct = (static_cast<float>(right_nonzeros) / pairs_read) * 100.0f;

        Serial.println("\r\n---------------------- [INMP441 DIAGNOSTIC STATUS] ----------------------");
        Serial.printf("Left Channel  (L/R->GND): Non-Zero: %5.1f%% | P2P: %8d | RMS: %5.3f | Raw[0]: 0x%08X\r\n",
                      left_pct, left_p2p, left_rms, sample_l_raw0);
        Serial.printf("Right Channel (L/R->3V3): Non-Zero: %5.1f%% | P2P: %8d | RMS: %5.3f | Raw[0]: 0x%08X\r\n",
                      right_pct, right_p2p, right_rms, sample_r_raw0);

        if (left_p2p > 500 || left_rms > 0.005f) {
            Serial.println(">>> RESULT: [PASS] ACTIVE AUDIO DETECTED ON LEFT CHANNEL (L/R -> GND).");
        } else if (right_p2p > 500 || right_rms > 0.005f) {
            Serial.println(">>> RESULT: [PASS] ACTIVE AUDIO DETECTED ON RIGHT CHANNEL (L/R -> 3V3).");
        } else {
            Serial.println(">>> RESULT: [FAIL] FLATLINE DETECTED (ALL ZEROS ON BOTH CHANNELS)!");
            Serial.println("    Troubleshooting Checklist:");
            Serial.println("    1. VDD Pin -> Must connect to 3V3 (Measure with multimeter: ~3.3V).");
            Serial.println("    2. GND Pin -> Must share ground with ESP32.");
            Serial.println("    3. L/R Pin -> Must be tied to GND (Left) or 3V3 (Right), NOT floating.");
            Serial.println("    4. SD  Pin -> GPIO 32 (Data line from microphone).");
            Serial.println("    5. WS  Pin -> GPIO 15 (Word Select / LRCLK clock line).");
            Serial.println("    6. SCK Pin -> GPIO 14 (Bit Clock / BCLK clock line).");
            Serial.println("    7. Check for cold solder joints on the INMP441 breakout pin header.");
        }
        Serial.println("-------------------------------------------------------------------------\r\n");
    }
}
