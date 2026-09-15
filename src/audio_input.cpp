/**
 * @file audio_input.cpp
 * @brief Implementation of I2S microphone driver for ESP32.
 */

#include "audio_input.h"
#include <esp_log.h>

static const char* TAG = "AudioInput";

AudioInput::AudioInput(i2s_port_t port)
    : _port(port), _isRunning(false) {}

AudioInput::~AudioInput() {
    end();
}

esp_err_t AudioInput::begin() {
    if (_isRunning) {
        ESP_LOGW(TAG, "I2S driver already running on port %d", _port);
        return ESP_OK;
    }

    const i2s_config_t i2s_config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = Config::AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = static_cast<int>(Config::AUDIO_DMA_BUF_COUNT),
        .dma_buf_len = static_cast<int>(Config::AUDIO_DMA_BUF_LEN),
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    const i2s_pin_config_t pin_config = {
        .bck_io_num = Config::PIN_I2S_SCK,
        .ws_io_num = Config::PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = Config::PIN_I2S_SD
    };

    esp_err_t err = i2s_driver_install(_port, &i2s_config, 0, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed installing I2S driver: 0x%x", err);
        return err;
    }

    err = i2s_set_pin(_port, &pin_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed setting I2S pins: 0x%x", err);
        i2s_driver_uninstall(_port);
        return err;
    }

    err = i2s_zero_dma_buffer(_port);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed zeroing DMA buffer: 0x%x", err);
    }

    _isRunning = true;
    ESP_LOGI(TAG, "I2S driver successfully initialized on port %d (SCK=%d, WS=%d, SD=%d)",
             _port, Config::PIN_I2S_SCK, Config::PIN_I2S_WS, Config::PIN_I2S_SD);
    return ESP_OK;
}

void AudioInput::end() {
    if (_isRunning) {
        i2s_stop(_port);
        i2s_driver_uninstall(_port);
        _isRunning = false;
        ESP_LOGI(TAG, "I2S driver stopped and uninstalled on port %d", _port);
    }
}

size_t AudioInput::read(int32_t* buffer, size_t samples_to_read, TickType_t timeout_ticks) {
    if (!_isRunning || buffer == nullptr || samples_to_read == 0) {
        return 0;
    }

    size_t bytes_to_read = samples_to_read * sizeof(int32_t);
    size_t bytes_read = 0;

    esp_err_t err = i2s_read(_port, buffer, bytes_to_read, &bytes_read, timeout_ticks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_read failed with error: 0x%x", err);
        return 0;
    }

    return bytes_read / sizeof(int32_t);
}

void AudioInput::pause() {
    if (_isRunning) {
        i2s_stop(_port);
    }
}

void AudioInput::resume() {
    if (_isRunning) {
        i2s_start(_port);
    }
}

