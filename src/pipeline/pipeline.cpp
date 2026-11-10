/**
 * @file pipeline.cpp
 * @brief Capture / process / transport tasks. See pipeline.h for the ownership model.
 */
#include "pipeline.h"

#include <esp_timer.h>
#include "audio_dsp.h"
#include "audio_input.h"
#include "sample_format.h"
#include "spectrum.h"
#include "spsc_ring.h"

namespace pipeline {
namespace {

static_assert((Config::FRAME_POOL_SIZE & (Config::FRAME_POOL_SIZE - 1)) == 0,
              "FRAME_POOL_SIZE must be a power of two");

using Ring = mic::SpscRing<AudioFrame*, Config::FRAME_POOL_SIZE>;

AudioFrame g_pool[Config::FRAME_POOL_SIZE];
Ring g_free_ring;  // transport -> capture
Ring g_proc_ring;  // capture   -> process
Ring g_tx_ring;    // process   -> transport
Stats g_stats;

AudioInput g_input;
TaskHandle_t g_process_task = nullptr;
TaskHandle_t g_transport_task = nullptr;

void update_max(std::atomic<uint32_t>& slot, uint32_t v) {
    uint32_t cur = slot.load(std::memory_order_relaxed);
    while (v > cur && !slot.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

// ---------------------------------------------------------------------------
// Stage 1: capture. Highest priority. Drains I2S DMA continuously, unpacks, removes
// DC and assembles FRAME_SAMPLES-sized frames.
// ---------------------------------------------------------------------------
void capture_task(void*) {
    constexpr size_t kChunk = Config::AUDIO_CHUNK_SAMPLES;
    int32_t s24[kChunk];
    float chunk[kChunk];
    mic::DcBlocker dc(mic::DcBlocker::coefficient(Config::DC_BLOCK_CUTOFF_HZ,
                                                  static_cast<float>(Config::AUDIO_SAMPLE_RATE)));
    const TickType_t timeout = pdMS_TO_TICKS(Config::I2S_READ_TIMEOUT_MS);

    AudioFrame* cur = nullptr;
    size_t fill = 0;
    uint32_t sample_index = 0;

    for (;;) {
        const size_t n = g_input.read(s24, kChunk, timeout);
        if (n == 0) {
            g_stats.i2s_timeouts.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        // Filter every sample, including those of dropped frames, so the DC blocker
        // state stays continuous and does not ring after a drop.
        for (size_t i = 0; i < n; ++i) chunk[i] = mic::s24_to_float(s24[i]);
        dc.process(chunk, n);

        for (size_t i = 0; i < n; ++i) {
            if (fill == 0) {  // frame boundary: try to claim a free frame
                if (g_free_ring.pop(cur)) {
                    cur->sample_index = sample_index;
                } else {
                    cur = nullptr;
                    g_stats.frames_dropped.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (cur) cur->samples[fill] = chunk[i];
            ++fill;
            ++sample_index;
            if (fill == Config::FRAME_SAMPLES) {
                if (cur) {
                    g_proc_ring.push(cur);  // cannot fail: capacity == pool size
                    g_stats.frames_captured.fetch_add(1, std::memory_order_relaxed);
                    xTaskNotifyGive(g_process_task);
                    cur = nullptr;
                }
                fill = 0;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 2: process. Level, FFT peak, VAD, int16 conversion. Only this task touches
// g_spectrum / g_vad, so they need no locking.
// ---------------------------------------------------------------------------
Spectrum g_spectrum;
mic::EnergyVad g_vad([] {
    mic::EnergyVad::Params p;
    p.threshold_db = Config::VAD_THRESHOLD_DB;
    p.min_level_dbfs = Config::VAD_MIN_LEVEL_DBFS;
    p.hangover_frames = Config::VAD_HANGOVER_FRAMES;
    p.floor_rise_db_per_frame = Config::VAD_FLOOR_RISE_DB;
    return p;
}());

void process_frame(AudioFrame& f) {
    constexpr size_t n = Config::FRAME_SAMPLES;
    constexpr float bin_hz = static_cast<float>(Config::AUDIO_SAMPLE_RATE) / n;
    const size_t min_bin = static_cast<size_t>(Config::PEAK_MIN_HZ / bin_hz + 0.5f);

    Features& ft = f.features;
    ft.rms_dbfs = mic::amplitude_to_dbfs(mic::rms(f.samples, n));
    ft.peak_dbfs = mic::amplitude_to_dbfs(mic::peak_abs(f.samples, n));

    g_spectrum.compute(f.samples);
    ft.peak_hz = mic::spectral_peak_hz(g_spectrum.power(), Spectrum::kBins, bin_hz, min_bin);

    ft.voice = g_vad.update(ft.rms_dbfs);
    ft.noise_floor_dbfs = g_vad.noise_floor_dbfs();

    for (size_t i = 0; i < n; ++i) f.pcm[i] = mic::float_to_s16(f.samples[i]);
}

void process_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        AudioFrame* f = nullptr;
        while (g_proc_ring.pop(f)) {
            const int64_t t0 = esp_timer_get_time();
            process_frame(*f);
            const uint32_t dt = static_cast<uint32_t>(esp_timer_get_time() - t0);
            g_stats.process_us_last.store(dt, std::memory_order_relaxed);
            update_max(g_stats.process_us_max, dt);
            g_stats.frames_processed.fetch_add(1, std::memory_order_relaxed);
            g_tx_ring.push(f);  // cannot fail
            xTaskNotifyGive(g_transport_task);
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 3: transport. Sends frames and returns them to the free ring. Also emits
// 1 Hz serial telemetry so the pipeline is observable without a network.
// ---------------------------------------------------------------------------
bool send_frame(const AudioFrame&) {
    return false;  // no network transport configured
}

// `ft` is a copy taken before the frame was recycled, owned by the transport task.
void print_stats(const Features& ft) {
    Serial.printf("[dsp ] rms=%.1f dBFS peak=%.1f dBFS f0=%.0f Hz floor=%.1f dBFS vad=%d\r\n",
                  ft.rms_dbfs, ft.peak_dbfs, ft.peak_hz, ft.noise_floor_dbfs, ft.voice ? 1 : 0);
    Serial.printf("[pipe] cap=%u drop=%u proc=%u sent=%u err=%u i2s_to=%u proc_us=%u/%u\r\n",
                  g_stats.frames_captured.load(), g_stats.frames_dropped.load(),
                  g_stats.frames_processed.load(), g_stats.frames_sent.load(),
                  g_stats.send_errors.load(), g_stats.i2s_timeouts.load(),
                  g_stats.process_us_last.load(), g_stats.process_us_max.load());
}

void transport_task(void*) {
    uint32_t last_stats_ms = millis();
    Features last_features;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(Config::STATS_INTERVAL_MS));
        AudioFrame* f = nullptr;
        while (g_tx_ring.pop(f)) {
            if (send_frame(*f)) {
                g_stats.frames_sent.fetch_add(1, std::memory_order_relaxed);
            }
            last_features = f->features;
            g_free_ring.push(f);  // cannot fail
        }
        const uint32_t now = millis();
        if (now - last_stats_ms >= Config::STATS_INTERVAL_MS) {  // wrap-safe
            last_stats_ms = now;
            print_stats(last_features);
        }
    }
}

}  // namespace

const Stats& stats() { return g_stats; }

bool start() {
    for (auto& f : g_pool) g_free_ring.push(&f);

    if (!g_spectrum.begin()) return false;

    if (g_input.begin() != ESP_OK) return false;

    // Consumers first, so producers always have a valid task handle to notify.
    if (xTaskCreatePinnedToCore(transport_task, "transport", Config::TRANSPORT_TASK_STACK, nullptr,
                                Config::TRANSPORT_TASK_PRIORITY, &g_transport_task,
                                Config::TRANSPORT_TASK_CORE) != pdPASS) return false;
    if (xTaskCreatePinnedToCore(process_task, "process", Config::PROCESS_TASK_STACK, nullptr,
                                Config::PROCESS_TASK_PRIORITY, &g_process_task,
                                Config::PROCESS_TASK_CORE) != pdPASS) return false;
    if (xTaskCreatePinnedToCore(capture_task, "capture", Config::CAPTURE_TASK_STACK, nullptr,
                                Config::CAPTURE_TASK_PRIORITY, nullptr,
                                Config::CAPTURE_TASK_CORE) != pdPASS) return false;
    return true;
}

}  // namespace pipeline
