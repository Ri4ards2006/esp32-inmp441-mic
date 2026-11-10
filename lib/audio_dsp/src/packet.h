/**
 * @file packet.h
 * @brief UDP wire format "MIC1" (pure C++, host-testable). One datagram per frame.
 *
 * All fields little-endian, written byte by byte (no struct packing / host-endianness
 * assumptions). Mirror: tools/receiver/mic_receiver.py (HEADER struct).
 *
 *  off size field
 *    0   4  magic            "MIC1"
 *    4   1  version          1
 *    5   1  flags            bit0 = VAD voice, bit1 = PCM payload present
 *    6   2  pcm_samples      number of int16 samples after the header (0 = features only)
 *    8   4  seq              datagram counter -> network loss = gaps in seq
 *   12   4  sample_index     index of the first sample of this frame since boot
 *                            -> capture drops = sample_index gaps with contiguous seq
 *   16   4  sample_rate      Hz
 *   20   4  rms_dbfs         float32
 *   24   4  peak_dbfs        float32
 *   28   4  peak_hz          float32
 *   32   4  noise_floor_dbfs float32
 *   36   4  frames_dropped   cumulative capture drops
 *   40 2*n  pcm              int16 s16le, mono
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mic {

constexpr uint8_t kPacketVersion = 1;
constexpr size_t kPacketHeaderSize = 40;
constexpr uint8_t kFlagVoice = 0x01;
constexpr uint8_t kFlagPcm = 0x02;

struct PacketFields {
    bool     voice = false;
    uint32_t seq = 0;
    uint32_t sample_index = 0;
    uint32_t sample_rate = 0;
    float    rms_dbfs = 0.0f;
    float    peak_dbfs = 0.0f;
    float    peak_hz = 0.0f;
    float    noise_floor_dbfs = 0.0f;
    uint32_t frames_dropped = 0;
};

constexpr size_t packet_size(size_t pcm_samples) {
    return kPacketHeaderSize + 2 * pcm_samples;
}

namespace detail {
inline uint8_t* put_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    return p + 2;
}
inline uint8_t* put_u32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
    return p + 4;
}
inline uint8_t* put_f32(uint8_t* p, float f) {
    uint32_t v;
    static_assert(sizeof(v) == sizeof(f), "IEEE-754 binary32 expected");
    std::memcpy(&v, &f, sizeof(v));
    return put_u32(p, v);
}
}  // namespace detail

/// Serialise one datagram. Returns bytes written, or 0 if `cap` is too small or
/// pcm_samples does not fit the 16-bit length field.
inline size_t encode_packet(const PacketFields& f, const int16_t* pcm, size_t pcm_samples,
                            uint8_t* out, size_t cap) {
    using namespace detail;
    if (pcm_samples > 0xFFFF || (pcm_samples > 0 && pcm == nullptr)) return 0;
    const size_t total = packet_size(pcm_samples);
    if (out == nullptr || cap < total) return 0;

    uint8_t* p = out;
    *p++ = 'M'; *p++ = 'I'; *p++ = 'C'; *p++ = '1';
    *p++ = kPacketVersion;
    *p++ = static_cast<uint8_t>((f.voice ? kFlagVoice : 0) | (pcm_samples ? kFlagPcm : 0));
    p = put_u16(p, static_cast<uint16_t>(pcm_samples));
    p = put_u32(p, f.seq);
    p = put_u32(p, f.sample_index);
    p = put_u32(p, f.sample_rate);
    p = put_f32(p, f.rms_dbfs);
    p = put_f32(p, f.peak_dbfs);
    p = put_f32(p, f.peak_hz);
    p = put_f32(p, f.noise_floor_dbfs);
    p = put_u32(p, f.frames_dropped);
    for (size_t i = 0; i < pcm_samples; ++i) p = put_u16(p, static_cast<uint16_t>(pcm[i]));
    return total;
}

}  // namespace mic
