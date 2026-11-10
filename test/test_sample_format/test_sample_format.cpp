// Host-side tests for INMP441 sample conversion and DC removal.
// Run with: pio test -e native
#include <unity.h>
#include <cmath>
#include "sample_format.h"

using namespace mic;

void setUp() {}
void tearDown() {}

// The INMP441 puts a 24-bit two's-complement word in bits [31:8] of the slot.
static int32_t slot_from_s24(int32_t s24) {
    return static_cast<int32_t>(static_cast<uint32_t>(s24) << 8);
}

void test_slot_to_s24_positive_full_scale() {
    TEST_ASSERT_EQUAL_INT32(0x7FFFFF, slot_to_s24(slot_from_s24(0x7FFFFF)));
}

void test_slot_to_s24_negative_full_scale_sign_extends() {
    TEST_ASSERT_EQUAL_INT32(-8388608, slot_to_s24(static_cast<int32_t>(0x80000000u)));
}

void test_slot_to_s24_minus_one() {
    TEST_ASSERT_EQUAL_INT32(-1, slot_to_s24(static_cast<int32_t>(0xFFFFFF00u)));
}

void test_slot_to_s24_discards_low_byte() {
    // Low 8 bits of the slot are not part of the sample (zero / don't care).
    TEST_ASSERT_EQUAL_INT32(1, slot_to_s24(static_cast<int32_t>(0x000001FFu)));
}

void test_s24_to_float_range_is_symmetric_power_of_two() {
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, s24_to_float(-8388608));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f - 1.0f / 8388608.0f, s24_to_float(8388607));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s24_to_float(0));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, s24_to_float(4194304));
}

void test_float_to_s16_rounds_and_clips() {
    TEST_ASSERT_EQUAL_INT16(0, float_to_s16(0.0f));
    TEST_ASSERT_EQUAL_INT16(16384, float_to_s16(0.5f));
    TEST_ASSERT_EQUAL_INT16(32767, float_to_s16(1.0f));
    TEST_ASSERT_EQUAL_INT16(32767, float_to_s16(3.0f));
    TEST_ASSERT_EQUAL_INT16(-32768, float_to_s16(-1.0f));
    TEST_ASSERT_EQUAL_INT16(-32768, float_to_s16(-3.0f));
}

void test_dc_blocker_coefficient_from_cutoff() {
    // R = exp(-2*pi*fc/fs)
    float r = DcBlocker::coefficient(20.0f, 16000.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, std::exp(-2.0f * 3.14159265f * 20.0f / 16000.0f), r);
    TEST_ASSERT_TRUE(r > 0.99f && r < 1.0f);
}

void test_dc_blocker_removes_constant_offset() {
    DcBlocker dc(DcBlocker::coefficient(20.0f, 16000.0f));
    float buf[256];
    float last = 1.0f;
    // 1 second of pure DC at 0.25 FS
    for (int block = 0; block < 16000 / 256 + 1; ++block) {
        for (float& v : buf) v = 0.25f;
        dc.process(buf, 256);
        last = buf[255];
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, last);
}

void test_dc_blocker_passes_1khz_sine_nearly_unchanged() {
    const float fs = 16000.0f;
    DcBlocker dc(DcBlocker::coefficient(20.0f, fs));
    const int n = 16000;
    double in_sq = 0, out_sq = 0;
    for (int i = 0; i < n; ++i) {
        float x = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * i / fs) + 0.1f;
        float y = x;
        dc.process(&y, 1);
        if (i >= n / 2) {  // skip settling
            float xs = x - 0.1f;
            in_sq += xs * xs;
            out_sq += y * y;
        }
    }
    float gain_db = 10.0f * std::log10(static_cast<float>(out_sq / in_sq));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, gain_db);
}

void test_dc_blocker_state_persists_across_calls() {
    DcBlocker a(0.995f), b(0.995f);
    float x[8] = {1, 2, 3, 4, -1, -2, -3, -4};
    float y1[8], y2[8];
    for (int i = 0; i < 8; ++i) { y1[i] = x[i]; y2[i] = x[i]; }
    a.process(y1, 8);
    b.process(y2, 3);
    b.process(y2 + 3, 5);
    for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_FLOAT(y1[i], y2[i]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_slot_to_s24_positive_full_scale);
    RUN_TEST(test_slot_to_s24_negative_full_scale_sign_extends);
    RUN_TEST(test_slot_to_s24_minus_one);
    RUN_TEST(test_slot_to_s24_discards_low_byte);
    RUN_TEST(test_s24_to_float_range_is_symmetric_power_of_two);
    RUN_TEST(test_float_to_s16_rounds_and_clips);
    RUN_TEST(test_dc_blocker_coefficient_from_cutoff);
    RUN_TEST(test_dc_blocker_removes_constant_offset);
    RUN_TEST(test_dc_blocker_passes_1khz_sine_nearly_unchanged);
    RUN_TEST(test_dc_blocker_state_persists_across_calls);
    return UNITY_END();
}
