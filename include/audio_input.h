/**
 * @file audio_input.h
 * @brief High-level modular driver interface for I2S microphone acquisition.
 */

#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

/**
 * @brief Manages I2S peripheral lifecycle, DMA ring buffers, and microphone sampling.
 */
class AudioInput {
public:
    AudioInput(i2s_port_t port = Config::I2S_PORT);
    ~AudioInput();

    /**
     * @brief Installs the I2S driver and configures GPIO pin routing.
     * @return ESP_OK on success, or appropriate esp_err_t code.
     */
    esp_err_t begin();

    /**
     * @brief Stops and uninstalls the I2S driver, releasing hardware resources.
     */
    void end();

    /**
     * @brief Read raw 32-bit audio samples from the I2S DMA queue.
     * @param[out] buffer Destination buffer for 32-bit samples.
     * @param[in] samples_to_read Total number of 32-bit samples requested.
     * @param[in] timeout_ticks FreeRTOS ticks to wait before timing out.
     * @return Number of samples successfully read.
     */
    size_t read(int32_t* buffer, size_t samples_to_read, TickType_t timeout_ticks = portMAX_DELAY);

    /**
     * @brief Temporarily pauses I2S DMA acquisition.
     */
    void pause();

    /**
     * @brief Resumes paused I2S DMA acquisition.
     */
    void resume();

    /**
     * @brief Check whether the I2S driver is actively configured and running.
     */
    bool isRunning() const { return _isRunning; }

private:
    i2s_port_t _port;
    bool _isRunning;
};

