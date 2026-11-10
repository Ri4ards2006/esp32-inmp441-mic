# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy>=1.24", "matplotlib>=3.7"]
# ///
"""Receiver for the ESP32 + INMP441 "MIC1" UDP stream.

Live spectrogram + optional WAV recording:

    uv run tools/receiver/mic_receiver.py --port 5005 --wav capture.wav

Headless (record only):

    uv run tools/receiver/mic_receiver.py --no-plot --wav capture.wav

Without hardware, a synthetic sweep can be sent to a running receiver:

    uv run tools/receiver/mic_receiver.py --simulate 127.0.0.1:5005

Wire format: see lib/audio_dsp/src/packet.h (mirrored by HEADER below).
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time
import wave
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

MAGIC = b"MIC1"
VERSION = 1
FLAG_VOICE = 0x01
FLAG_PCM = 0x02
# magic, version, flags, pcm_samples, seq, sample_index, sample_rate,
# rms_dbfs, peak_dbfs, peak_hz, noise_floor_dbfs, frames_dropped
HEADER = struct.Struct("<4sBBHIIIffffI")
assert HEADER.size == 40

# A seq that is at most this far behind the last one is a late/duplicate datagram;
# further behind means the device restarted.
REORDER_WINDOW = 64


@dataclass
class Packet:
    voice: bool
    seq: int
    sample_index: int
    sample_rate: int
    rms_dbfs: float
    peak_dbfs: float
    peak_hz: float
    noise_floor_dbfs: float
    frames_dropped: int
    pcm: np.ndarray  # int16, mono


def decode_packet(data: bytes) -> Packet:
    if len(data) < HEADER.size:
        raise ValueError(f"short datagram ({len(data)} B)")
    (magic, ver, flags, n, seq, idx, fs, rms, peak, f0, floor, drops) = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ValueError(f"bad magic {magic!r}")
    if ver != VERSION:
        raise ValueError(f"unsupported version {ver}")
    if len(data) != HEADER.size + 2 * n:
        raise ValueError(f"length {len(data)} != header + {n} samples")
    pcm = np.frombuffer(data, dtype="<i2", count=n, offset=HEADER.size).astype(np.int16)
    return Packet(bool(flags & FLAG_VOICE), seq, idx, fs, rms, peak, f0, floor, drops, pcm)


def encode_packet(p: Packet) -> bytes:
    n = len(p.pcm)
    flags = (FLAG_VOICE if p.voice else 0) | (FLAG_PCM if n else 0)
    head = HEADER.pack(MAGIC, VERSION, flags, n, p.seq & 0xFFFFFFFF, p.sample_index & 0xFFFFFFFF,
                       p.sample_rate, p.rms_dbfs, p.peak_dbfs, p.peak_hz, p.noise_floor_dbfs,
                       p.frames_dropped)
    return head + np.asarray(p.pcm, dtype="<i2").tobytes()


@dataclass
class StreamAssembler:
    """Turns datagrams into a continuous PCM timeline.

    - seq gaps        -> lost_packets (network loss)
    - sample_index gaps -> gap_samples (network loss *or* device-side capture drops),
      filled with silence (capped at max_gap_s) so the WAV keeps real-time length.
    - counters going backwards -> restart (no gap fill); small seq regressions inside
      REORDER_WINDOW are late/duplicate datagrams and are discarded.
    """

    max_gap_s: float = 1.0
    packets: int = 0
    lost_packets: int = 0
    gap_samples: int = 0
    late_packets: int = 0
    restarts: int = 0
    _last_seq: int | None = None
    _expected_idx: int | None = None

    def push(self, p: Packet) -> list[np.ndarray]:
        out: list[np.ndarray] = []
        self.packets += 1
        if self._last_seq is not None:
            ahead = (p.seq - self._last_seq) & 0xFFFFFFFF
            behind = (self._last_seq - p.seq) & 0xFFFFFFFF
            if 0 < behind <= REORDER_WINDOW and p.seq != 0:
                self.late_packets += 1
                return out
            if p.seq == 0 or (behind > REORDER_WINDOW and ahead >= 0x80000000):
                self.restarts += 1
                self._expected_idx = None
            elif ahead > 1:
                self.lost_packets += ahead - 1
        if self._expected_idx is not None and p.sample_index > self._expected_idx:
            gap = p.sample_index - self._expected_idx
            self.gap_samples += gap
            fill = min(gap, int(self.max_gap_s * p.sample_rate))
            if fill:
                out.append(np.zeros(fill, dtype=np.int16))
        out.append(p.pcm)
        self._last_seq = p.seq
        self._expected_idx = p.sample_index + len(p.pcm)
        return out


def spectrum_column_db(pcm: np.ndarray) -> np.ndarray:
    """Hann-windowed magnitude spectrum in dBFS (a full-scale sine peaks near 0 dB)."""
    x = pcm.astype(np.float32) / 32768.0
    w = np.hanning(len(x)).astype(np.float32)
    mag = np.abs(np.fft.rfft(x * w)) / (w.sum() / 2.0)
    return 20.0 * np.log10(mag + 1e-9)


class WavSink:
    def __init__(self, path: Path | None):
        self.path = path
        self._w: wave.Wave_write | None = None

    def write(self, fs: int, chunks: list[np.ndarray]) -> None:
        if self.path is None:
            return
        if self._w is None:
            self._w = wave.open(str(self.path), "wb")
            self._w.setnchannels(1)
            self._w.setsampwidth(2)
            self._w.setframerate(fs)
        for c in chunks:
            self._w.writeframes(np.asarray(c, dtype="<i2").tobytes())

    def close(self) -> None:
        if self._w is not None:
            self._w.close()
            self._w = None


def receive(sock: socket.socket, wav_path: Path | None = None, max_packets: int | None = None,
            timeout_s: float | None = None, on_packet=None) -> StreamAssembler:
    """Blocking receive loop (headless). Stops after max_packets, on timeout, or Ctrl+C."""
    asm = StreamAssembler()
    sink = WavSink(wav_path)
    sock.settimeout(timeout_s)
    try:
        while max_packets is None or asm.packets < max_packets:
            try:
                data = sock.recv(65535)
            except socket.timeout:
                break
            try:
                p = decode_packet(data)
            except ValueError as e:
                print(f"[rx] drop malformed datagram: {e}", file=sys.stderr)
                continue
            sink.write(p.sample_rate, asm.push(p))
            if on_packet:
                on_packet(p, asm)
    except KeyboardInterrupt:
        pass
    finally:
        sink.close()
    return asm


def live_plot(sock: socket.socket, wav_path: Path | None, window_s: float) -> StreamAssembler:
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation

    asm = StreamAssembler()
    sink = WavSink(wav_path)
    sock.setblocking(False)
    state: dict = {"img": None, "fs": None, "cols": None, "last": None}

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.set_xlabel("time [s]")
    ax.set_ylabel("frequency [Hz]")

    def update(_):
        while True:
            try:
                data = sock.recv(65535)
            except BlockingIOError:
                break
            try:
                p = decode_packet(data)
            except ValueError:
                continue
            sink.write(p.sample_rate, asm.push(p))
            state["last"] = p
            if len(p.pcm) == 0:
                continue
            if state["img"] is None:
                n = len(p.pcm)
                ncols = max(1, int(window_s * p.sample_rate / n))
                state["cols"] = np.full((n // 2 + 1, ncols), -120.0, dtype=np.float32)
                state["img"] = ax.imshow(state["cols"], origin="lower", aspect="auto",
                                         extent=(-window_s, 0, 0, p.sample_rate / 2),
                                         vmin=-110, vmax=-10, cmap="magma")
                fig.colorbar(state["img"], ax=ax, label="dBFS")
            cols = state["cols"]
            cols[:, :-1] = cols[:, 1:]
            cols[:, -1] = spectrum_column_db(p.pcm)
        if state["img"] is not None:
            state["img"].set_data(state["cols"])
        p = state["last"]
        if p is not None:
            ax.set_title(
                f"rms {p.rms_dbfs:6.1f} dBFS  f0 {p.peak_hz:6.0f} Hz  "
                f"floor {p.noise_floor_dbfs:6.1f}  VAD {'ON ' if p.voice else 'off'}  |  "
                f"lost {asm.lost_packets}  gaps {asm.gap_samples} smp  "
                f"dev-drops {p.frames_dropped}", fontsize=9)
        return []

    anim = FuncAnimation(fig, update, interval=50, cache_frame_data=False)  # noqa: F841
    try:
        plt.show()
    finally:
        sink.close()
    return asm


def simulate(target: str, seconds: float, fs: int = 16000, n: int = 512) -> None:
    """Send a synthetic log sweep 200 Hz -> 4 kHz at real-time pace (no hardware)."""
    host, _, port = target.partition(":")
    addr = (host, int(port or 5005))
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    frames = int(seconds * fs / n)
    t0 = time.monotonic()
    phase = 0.0
    for i in range(frames):
        f = 200.0 * (4000.0 / 200.0) ** (i / max(1, frames - 1))
        ph = phase + 2 * np.pi * f * np.arange(n) / fs
        phase = float(ph[-1] + 2 * np.pi * f / fs)
        x = 0.25 * np.sin(ph)
        pcm = np.round(x * 32767).astype(np.int16)
        rms = float(np.sqrt(np.mean(x**2)))
        p = Packet(True, i, i * n, fs, 20 * np.log10(rms), 20 * np.log10(0.25), f, -90.0, 0, pcm)
        tx.sendto(encode_packet(p), addr)
        sleep = t0 + (i + 1) * n / fs - time.monotonic()
        if sleep > 0:
            time.sleep(sleep)
    tx.close()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bind", default="0.0.0.0", help="local address (default 0.0.0.0)")
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--wav", type=Path, help="write received PCM to this WAV file")
    ap.add_argument("--no-plot", action="store_true", help="headless: record/print only")
    ap.add_argument("--window", type=float, default=10.0, help="spectrogram width in s")
    ap.add_argument("--max-packets", type=int, help="stop after N datagrams (headless)")
    ap.add_argument("--simulate", metavar="HOST[:PORT]", help="send a test sweep instead")
    ap.add_argument("--seconds", type=float, default=10.0, help="duration for --simulate")
    a = ap.parse_args(argv)

    if a.simulate:
        simulate(a.simulate, a.seconds)
        return 0

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((a.bind, a.port))
    print(f"[rx] listening on {a.bind}:{a.port}" + (f", recording to {a.wav}" if a.wav else ""))

    if a.no_plot:
        last = [0.0]

        def report(p: Packet, asm: StreamAssembler) -> None:
            now = time.monotonic()
            if now - last[0] >= 1.0:
                last[0] = now
                print(f"[rx] pkts={asm.packets} lost={asm.lost_packets} gap={asm.gap_samples} "
                      f"rms={p.rms_dbfs:.1f} dBFS f0={p.peak_hz:.0f} Hz vad={int(p.voice)} "
                      f"dev_drops={p.frames_dropped}")

        asm = receive(sock, a.wav, a.max_packets, None, report)
    else:
        asm = live_plot(sock, a.wav, a.window)
    print(f"[rx] done: packets={asm.packets} lost={asm.lost_packets} late={asm.late_packets} "
          f"gap_samples={asm.gap_samples} restarts={asm.restarts}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
