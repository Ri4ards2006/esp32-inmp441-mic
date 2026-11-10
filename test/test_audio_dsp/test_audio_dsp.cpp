// Host-side tests for level metering, spectral peak picking and the energy VAD.
// Run with: pio test -e native
#include <unity.h>
#include <cmath>
#include <vector>
#include "audio_dsp.h"

using namespace mic;

static constexpr float kPi = 3.14159265358979f;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- level -------
void test_rms_of_full_scale_sine_is_inv_sqrt2() {
    std::vector<float> x(1600);
    for (size_t i = 0; i < x.size(); ++i) x[i] = std::sin(2 * kPi * 1000.0f * i / 16000.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.70710678f, rms(x.data(), x.size()));
}

void test_peak_abs_finds_negative_peak() {
    float x[5] = {0.1f, -0.9f, 0.5f, 0.0f, 0.3f};
    TEST_ASSERT_EQUAL_FLOAT(0.9f, peak_abs(x, 5));
}

void test_dbfs_conversion_and_floor() {
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, amplitude_to_dbfs(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -6.0206f, amplitude_to_dbfs(0.5f));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, -3.0103f, amplitude_to_dbfs(0.70710678f));
    TEST_ASSERT_EQUAL_FLOAT(kDbfsFloor, amplitude_to_dbfs(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(kDbfsFloor, amplitude_to_dbfs(1e-9f));
}

void test_empty_input_is_safe() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rms(nullptr, 0));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, peak_abs(nullptr, 0));
}

// ------------------------------------------------------------- spectrum ------
// Reference power spectrum via naive DFT of a Hann-windowed frame (host only).
static std::vector<float> hann_power_spectrum(const std::vector<float>& x) {
    const size_t n = x.size();
    std::vector<float> p(n / 2 + 1);
    for (size_t k = 0; k <= n / 2; ++k) {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
            double v = x[i] * w;
            re += v * std::cos(2 * M_PI * k * i / n);
            im -= v * std::sin(2 * M_PI * k * i / n);
        }
        p[k] = static_cast<float>(re * re + im * im);
    }
    return p;
}

static void check_peak(float f_hz) {
    const size_t n = 512;
    const float fs = 16000.0f;
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = 0.5f * std::sin(2 * kPi * f_hz * i / fs);
    auto p = hann_power_spectrum(x);
    float est = spectral_peak_hz(p.data(), p.size(), fs / n, 2);
    TEST_ASSERT_FLOAT_WITHIN(3.0f, f_hz, est);  // bin width is 31.25 Hz
}

void test_peak_on_bin_center() { check_peak(1000.0f); }      // bin 32 exactly
void test_peak_between_bins() { check_peak(1234.5f); }
void test_peak_high_frequency() { check_peak(6789.0f); }

void test_peak_ignores_bins_below_min_bin() {
    float p[8] = {100.0f, 50.0f, 1.0f, 2.0f, 9.0f, 2.0f, 1.0f, 0.5f};
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 4.0f, spectral_peak_hz(p, 8, 1.0f, 2));
}

void test_peak_of_silence_is_zero() {
    float p[8] = {0};
    TEST_ASSERT_EQUAL_FLOAT(0.0f, spectral_peak_hz(p, 8, 1.0f, 1));
}

// ------------------------------------------------------------------ VAD ------
static EnergyVad::Params vad_params() {
    EnergyVad::Params p;
    p.threshold_db = 10.0f;
    p.min_level_dbfs = -80.0f;
    p.hangover_frames = 3;
    p.floor_rise_db_per_frame = 0.1f;
    return p;
}

void test_vad_quiet_noise_is_not_voice() {
    EnergyVad vad(vad_params());
    for (int i = 0; i < 100; ++i) TEST_ASSERT_FALSE(vad.update(-60.0f + (i % 3) * 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(1.5f, -60.0f, vad.noise_floor_dbfs());
}

void test_vad_burst_above_floor_triggers_then_hangover_releases() {
    EnergyVad vad(vad_params());
    for (int i = 0; i < 50; ++i) vad.update(-60.0f);
    TEST_ASSERT_TRUE(vad.update(-30.0f));
    TEST_ASSERT_TRUE(vad.update(-30.0f));
    // burst ends: 3 hangover frames stay active, then off
    TEST_ASSERT_TRUE(vad.update(-60.0f));
    TEST_ASSERT_TRUE(vad.update(-60.0f));
    TEST_ASSERT_TRUE(vad.update(-60.0f));
    TEST_ASSERT_FALSE(vad.update(-60.0f));
}

void test_vad_ignores_events_below_absolute_minimum() {
    EnergyVad vad(vad_params());
    for (int i = 0; i < 50; ++i) vad.update(-110.0f);
    TEST_ASSERT_FALSE(vad.update(-85.0f));  // +25 dB over floor but below -80 dBFS
}

void test_vad_floor_tracks_slow_rise_without_latching() {
    EnergyVad vad(vad_params());
    for (int i = 0; i < 50; ++i) vad.update(-60.0f);
    // ambient rises by 0.05 dB/frame (slower than floor rise of 0.1 dB/frame)
    float level = -60.0f;
    bool any_voice = false;
    for (int i = 0; i < 400; ++i) {
        level += 0.05f;
        any_voice |= vad.update(level);
    }
    TEST_ASSERT_FALSE(any_voice);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, level, vad.noise_floor_dbfs());
}

void test_vad_floor_drops_immediately_to_quieter_level() {
    EnergyVad vad(vad_params());
    for (int i = 0; i < 10; ++i) vad.update(-50.0f);
    vad.update(-70.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -70.0f, vad.noise_floor_dbfs());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rms_of_full_scale_sine_is_inv_sqrt2);
    RUN_TEST(test_peak_abs_finds_negative_peak);
    RUN_TEST(test_dbfs_conversion_and_floor);
    RUN_TEST(test_empty_input_is_safe);
    RUN_TEST(test_peak_on_bin_center);
    RUN_TEST(test_peak_between_bins);
    RUN_TEST(test_peak_high_frequency);
    RUN_TEST(test_peak_ignores_bins_below_min_bin);
    RUN_TEST(test_peak_of_silence_is_zero);
    RUN_TEST(test_vad_quiet_noise_is_not_voice);
    RUN_TEST(test_vad_burst_above_floor_triggers_then_hangover_releases);
    RUN_TEST(test_vad_ignores_events_below_absolute_minimum);
    RUN_TEST(test_vad_floor_tracks_slow_rise_without_latching);
    RUN_TEST(test_vad_floor_drops_immediately_to_quieter_level);
    return UNITY_END();
}
