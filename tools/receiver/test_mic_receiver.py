"""Tests for the MIC1 receiver. Run: uv run --with numpy --with pytest pytest tools/receiver -q"""

import socket
import threading
import wave

import numpy as np
import pytest

import mic_receiver as mr

# Same golden vector as test/test_packet/test_packet.cpp (firmware encoder).
GOLDEN = bytes.fromhex(
    "4d494331010302000403020100100000803e00000000a0c10000c0c000007a44"
    "000070c20700000001 00feff".replace(" ", "")
)


def test_decode_golden_vector_from_firmware():
    p = mr.decode_packet(GOLDEN)
    assert p.voice is True
    assert p.seq == 0x01020304
    assert p.sample_index == 0x1000
    assert p.sample_rate == 16000
    assert p.rms_dbfs == pytest.approx(-20.0)
    assert p.peak_dbfs == pytest.approx(-6.0)
    assert p.peak_hz == pytest.approx(1000.0)
    assert p.noise_floor_dbfs == pytest.approx(-60.0)
    assert p.frames_dropped == 7
    assert p.pcm.tolist() == [1, -2]


def test_encode_is_inverse_of_decode():
    assert mr.encode_packet(mr.decode_packet(GOLDEN)) == GOLDEN


@pytest.mark.parametrize(
    "data",
    [b"", b"XXXX" + GOLDEN[4:], GOLDEN[:4] + b"\x02" + GOLDEN[5:], GOLDEN[:-1]],
    ids=["empty", "bad-magic", "bad-version", "truncated"],
)
def test_decode_rejects_malformed(data):
    with pytest.raises(ValueError):
        mr.decode_packet(data)


def _pkt(seq, idx, n=4, value=100, fs=16000):
    return mr.Packet(
        voice=False, seq=seq, sample_index=idx, sample_rate=fs,
        rms_dbfs=-30.0, peak_dbfs=-20.0, peak_hz=440.0, noise_floor_dbfs=-70.0,
        frames_dropped=0, pcm=np.full(n, value, dtype=np.int16),
    )


def test_stream_counts_network_loss_and_fills_gaps_with_silence():
    s = mr.StreamAssembler()
    out = []
    out += s.push(_pkt(0, 0))
    out += s.push(_pkt(2, 8))  # seq 1 (samples 4..7) lost in the network
    assert s.lost_packets == 1
    pcm = np.concatenate(out)
    assert pcm.tolist() == [100] * 4 + [0] * 4 + [100] * 4


def test_stream_detects_capture_drop_without_network_loss():
    s = mr.StreamAssembler()
    s.push(_pkt(0, 0))
    s.push(_pkt(1, 8))  # contiguous seq, but 4 samples missing -> device-side drop
    assert s.lost_packets == 0
    assert s.gap_samples == 4


def test_stream_restart_does_not_insert_huge_gap():
    s = mr.StreamAssembler()
    s.push(_pkt(100, 400_000))
    out = s.push(_pkt(0, 0))  # device rebooted: counters went backwards
    assert s.restarts == 1
    assert sum(len(c) for c in out) == 4


def test_gap_fill_is_capped():
    s = mr.StreamAssembler(max_gap_s=0.001)  # 16 samples @ 16 kHz
    s.push(_pkt(0, 0))
    out = s.push(_pkt(1, 10_000))
    assert sum(len(c) for c in out) == 16 + 4


def test_spectrogram_column_peaks_at_tone_frequency():
    fs, n = 16000, 512
    t = np.arange(n) / fs
    pcm = (0.5 * 32767 * np.sin(2 * np.pi * 1000 * t)).astype(np.int16)
    col = mr.spectrum_column_db(pcm)
    assert col.shape == (n // 2 + 1,)
    assert abs(np.argmax(col) * fs / n - 1000) <= fs / n


def test_udp_loopback_writes_wav(tmp_path):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    wav_path = tmp_path / "out.wav"
    result = {}

    def run():
        result["stats"] = mr.receive(sock, wav_path=wav_path, max_packets=5, timeout_s=2.0)

    th = threading.Thread(target=run)
    th.start()
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for i in range(5):
        tx.sendto(mr.encode_packet(_pkt(i, i * 512, n=512, value=i)), ("127.0.0.1", port))
    th.join(5)
    sock.close()
    tx.close()

    assert result["stats"].packets == 5
    with wave.open(str(wav_path), "rb") as w:
        assert w.getframerate() == 16000
        assert w.getsampwidth() == 2
        assert w.getnchannels() == 1
        assert w.getnframes() == 5 * 512
