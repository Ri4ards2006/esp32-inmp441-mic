/**
 * @file audio_input.cpp
 * @brief Implementation of verified I2S microphone driver for INMP441 on ESP32.
 */

#include "audio_input.h"
#include <esp_log.h>

static const char* TAG = "AudioInput";

AudioInput::AudioInput(i2s_port_t port)
    : _port(port), _isRunning(false), _stereo_buf(nullptr), _stereo_buf_capacity(0) {}

AudioInput::~AudioInput() {
    end();
}

esp_err_t AudioInput::begin() {
    if (_isRunning) {
        return ESP_OK;
    }

    // Allocate persistent stereo DMA buffer for channel de-interleaving
    _stereo_buf_capacity = Config::AUDIO_CHUNK_SAMPLES * 2;
    _stereo_buf = new (std::nothrow) int32_t[_stereo_buf_capacity];
    if (!_stereo_buf) {
        ESP_LOGE(TAG, "Failed allocating internal stereo buffer");
        return ESP_ERR_NO_MEM;
    }

    // Proven working I2S configuration for INMP441:
    // Capturing Stereo (RIGHT_LEFT) bypasses ESP32 mono-mode channel-swapping quirks
    const i2s_config_t i2s_config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = Config::AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = static_cast<i2s_comm_format_t>(I2S_COMM_FORMAT_STAND_I2S),
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
        delete[] _stereo_buf;
        _stereo_buf = nullptr;
        return err;
    }

    err = i2s_set_pin(_port, &pin_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed setting I2S pins: 0x%x", err);
        i2s_driver_uninstall(_port);
        delete[] _stereo_buf;
        _stereo_buf = nullptr;
        return err;
    }

    i2s_zero_dma_buffer(_port);
    _isRunning = true;

    if (!Config::STREAM_RAW_SAMPLES) {
        ESP_LOGI(TAG, "I2S driver initialized on port %d (SCK=%d, WS=%d, SD=%d)",
                 _port, Config::PIN_I2S_SCK, Config::PIN_I2S_WS, Config::PIN_I2S_SD);
    }
    return ESP_OK;
}

void AudioInput::end() {
    if (_isRunning) {
        i2s_stop(_port);
        i2s_driver_uninstall(_port);
        _isRunning = false;
    }
    if (_stereo_buf) {
        delete[] _stereo_buf;
        _stereo_buf = nullptr;
        _stereo_buf_capacity = 0;
    }
}

size_t AudioInput::read(int32_t* buffer, size_t samples_to_read, TickType_t timeout_ticks) {
    if (!_isRunning || buffer == nullptr || samples_to_read == 0 || _stereo_buf == nullptr) {
        return 0;
    }

    size_t pairs_to_read = samples_to_read;
    if (pairs_to_read * 2 > _stereo_buf_capacity) {
        pairs_to_read = _stereo_buf_capacity / 2;
    }

    size_t bytes_to_read = pairs_to_read * 2 * sizeof(int32_t);
    size_t bytes_read = 0;

    esp_err_t err = i2s_read(_port, _stereo_buf, bytes_to_read, &bytes_read, timeout_ticks);
    if (err != ESP_OK || bytes_read == 0) {
        return 0;
    }

    size_t pairs_read = bytes_read / (2 * sizeof(int32_t));
    for (size_t i = 0; i < pairs_read; ++i) {
        // INMP441 with L/R tied to GND is on Left subframe (index 2*i)
        // 24-bit MSB-justified inside 32-bit slot: shift right by 8 for signed 24-bit sample
        buffer[i] = _stereo_buf[2 * i] >> 8;
    }

    return pairs_read;
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
