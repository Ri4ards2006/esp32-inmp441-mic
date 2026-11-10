// Host-side tests for the UDP wire format. The golden vector below is duplicated in
// tools/receiver/test_mic_receiver.py so firmware and receiver cannot drift apart.
// Run with: pio test -e native
#include <unity.h>
#include <cstring>
#include "packet.h"

using namespace mic;

void setUp() {}
void tearDown() {}

static PacketFields golden_fields() {
    PacketFields f;
    f.voice = true;
    f.seq = 0x01020304u;
    f.sample_index = 0x00001000u;
    f.sample_rate = 16000;
    f.rms_dbfs = -20.0f;
    f.peak_dbfs = -6.0f;
    f.peak_hz = 1000.0f;
    f.noise_floor_dbfs = -60.0f;
    f.frames_dropped = 7;
    return f;
}

// "MIC1", ver 1, flags 0x03 (voice|pcm), n=2, seq, idx, fs, 4 floats, drops, pcm {1,-2}
static const uint8_t kGolden[] = {
    0x4D, 0x49, 0x43, 0x31, 0x01, 0x03, 0x02, 0x00,
    0x04, 0x03, 0x02, 0x01, 0x00, 0x10, 0x00, 0x00,
    0x80, 0x3E, 0x00, 0x00, 0x00, 0x00, 0xA0, 0xC1,
    0x00, 0x00, 0xC0, 0xC0, 0x00, 0x00, 0x7A, 0x44,
    0x00, 0x00, 0x70, 0xC2, 0x07, 0x00, 0x00, 0x00,
    0x01, 0x00, 0xFE, 0xFF,
};

void test_header_size_is_40() {
    TEST_ASSERT_EQUAL_UINT32(40, kPacketHeaderSize);
}

void test_encode_matches_golden_vector() {
    const int16_t pcm[2] = {1, -2};
    uint8_t out[64];
    const size_t n = encode_packet(golden_fields(), pcm, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(sizeof(kGolden), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kGolden, out, sizeof(kGolden));
}

void test_features_only_packet_has_no_pcm_flag() {
    PacketFields f = golden_fields();
    f.voice = false;
    uint8_t out[64];
    const size_t n = encode_packet(f, nullptr, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(kPacketHeaderSize, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[6]);
}

void test_encode_refuses_too_small_buffer() {
    const int16_t pcm[2] = {1, -2};
    uint8_t out[43];
    TEST_ASSERT_EQUAL_UINT32(0, encode_packet(golden_fields(), pcm, 2, out, sizeof(out)));
}

void test_full_frame_fits_in_one_udp_datagram() {
    // 512 samples -> 1064 bytes, below the 1472-byte UDP payload of a 1500 MTU.
    TEST_ASSERT_TRUE(packet_size(512) <= 1472);
    TEST_ASSERT_EQUAL_UINT32(1064, packet_size(512));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_header_size_is_40);
    RUN_TEST(test_encode_matches_golden_vector);
    RUN_TEST(test_features_only_packet_has_no_pcm_flag);
    RUN_TEST(test_encode_refuses_too_small_buffer);
    RUN_TEST(test_full_frame_fits_in_one_udp_datagram);
    return UNITY_END();
}
