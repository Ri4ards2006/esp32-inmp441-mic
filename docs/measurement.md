# Measurement procedure — noise floor & frequency response

This document defines **how** to characterise the ESP32 + INMP441 chain. It contains
no measured results: every result field is `TODO` until it has been measured on real
hardware with the procedure below. Numbers that appear here are either datasheet
values (marked *datasheet*) or derived by calculation (marked *calculated*).

## 0. Conventions

| Item | Value |
| --- | --- |
| dBFS | `20·log10(RMS)` with digital full scale = 1.0 (see `lib/audio_dsp/src/audio_dsp.h`). A full-scale **sine** reads **−3.01 dBFS**. Datasheets that reference to a full-scale sine read 3.01 dB higher for the same signal. |
| Device level | `rms_dbfs` of each 512-sample frame (32 ms), computed in float from 24-bit data, after the DC blocker |
| WAV level | int16 PCM from the receiver. 16-bit quantisation noise ≈ −101 dBFS (*calculated*: `20·log10(2^-15/√12)`) |
| DC blocker | first-order high-pass, fc = 20 Hz. Its own attenuation (*calculated*, `f/√(f²+fc²)`): −0.64 dB @ 50 Hz, −0.17 dB @ 100 Hz, < 0.05 dB above 200 Hz |
| Analysis band | 20 Hz … 8 kHz (fs = 16 kHz) |

Reference values to compare against (*datasheet*, verify against your INMP441 datasheet
revision): sensitivity −26 dBFS @ 94 dB SPL, 1 kHz; SNR 61 dBA. Together these imply an
A-weighted self-noise around −26 − 61 = −87 dBFS (*calculated*, datasheet dBFS
convention), i.e. ~14 dB above the int16 quantisation floor.

## 1. Equipment

- ESP32 + INMP441 wired as in the README, firmware `env:esp32dev`, `include/secrets.h` set.
- PC on the same LAN running `tools/receiver/mic_receiver.py`.
- For the noise floor: the quietest room available; optionally a closed box lined with
  acoustic foam to separate mic self-noise from room noise.
- For the frequency response: a loudspeaker with reasonably flat response in the band,
  a **calibrated reference measurement microphone** with its calibration file, and an
  audio interface. Optional: a 94 dB SPL / 1 kHz acoustic calibrator for sensitivity.
- A tone generator (e.g. `sox`, REW, or any DAW) that can play stepped sines and a log sweep.

Record for every session: date, firmware commit (`git rev-parse --short HEAD`),
board, supply (USB port / power bank), wire length, room, mic orientation.

## 2. Noise floor

### 2.1 Capture

1. Power the board, wait 60 s (thermal settle, VAD floor converges).
2. Confirm the serial line `[pipe] ... drop=0` stays at 0 drops.
3. Record three 60 s takes, re-positioning nothing:
   ```bash
   uv run tools/receiver/mic_receiver.py --no-plot --wav noise_wifi_1.wav
   ```
   Stop with Ctrl+C after ≥ 60 s. Keep the serial monitor open and save its log
   (`pio device monitor | tee noise_wifi_1.log`) — it holds the device-side
   `rms_dbfs` from 24-bit data.
4. Repeat with the mic inside the foam box (if available): `noise_box_N.wav`.
5. Coupling check: WiFi TX bursts (31 datagrams/s) can couple into the 3.3 V rail.
   Build once **without** `include/secrets.h` (no WiFi) and log the serial `[dsp]`
   lines for 60 s. Compare device `rms_dbfs` WiFi on vs. off.

### 2.2 Analysis

For each WAV, discard the first 2 s (DC blocker / receiver start-up), then:

1. Unweighted level: RMS over 1 s windows → dBFS; report the **median** and min/max.
2. A-weighted level: apply the IEC 61672 A-curve in the frequency domain
   (`R_A(f) = 12194²·f⁴ / ((f²+20.6²)·√((f²+107.7²)(f²+737.9²))·(f²+12194²))`,
   `A(f) = 20·log10(R_A(f)) + 2.00 dB`) to an FFT of each window, then RMS → dBFS(A).
3. Noise spectrum: Welch PSD (Hann, nfft 4096, 50 % overlap). List discrete spurs
   (frequency, level) — typical suspects are WiFi frame rate harmonics and the
   USB supply.
4. Cross-check: device `rms_dbfs` (24-bit) vs. WAV level (16-bit). If the WAV reads
   notably lower/higher, the int16 path is limiting or clipping.

### 2.3 Results

| Condition | Unweighted dBFS (median / min / max) | A-weighted dBFS(A) | Device `rms_dbfs` | Spurs |
| --- | --- | --- | --- | --- |
| Room, WiFi on | TODO | TODO | TODO | TODO |
| Foam box, WiFi on | TODO | TODO | TODO | TODO |
| Room, WiFi off (serial only) | n/a | n/a | TODO | n/a |

## 3. Frequency response (substitution method)

### 3.1 Set-up

- Speaker and microphone on-axis, 30–50 cm apart, same height, away from walls.
- Mark the mic position precisely. The INMP441 and the reference mic are measured
  **one after the other at the same marked position** (substitution), with the same
  speaker drive level.
- Choose the drive level so the 1 kHz tone reads around −30 dBFS on the device and the
  sample peak never exceeds −6 dBFS at any frequency.

### 3.2 Stepped sine (primary)

1. Frequencies: 1/3-octave centres 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
   630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300 Hz (6.3 kHz is the
   last centre safely below fs/2 = 8 kHz).
2. Play each tone for 3 s. Record the whole sequence once with the INMP441
   (`--wav fr_dut_N.wav`) and once with the reference mic through the audio interface
   (`fr_ref_N.wav`), same order and timing.
3. While recording the DUT, check the serial `[dsp] f0=` value matches the played
   frequency (± one 31.25 Hz bin); a mismatch indicates distortion, a mains/WiFi spur
   or a wrong segment.
4. Repeat three times (N = 1..3), re-placing the mic between runs.

### 3.3 Analysis

For each tone segment (skip 0.5 s at each edge):

1. Hann-windowed FFT over 1 s blocks; tone level = peak bin magnitude (with parabolic
   interpolation), averaged over the blocks → `L_dut(f)`, `L_ref(f)` in dB.
2. Apply the reference mic calibration: `L_ref_corr(f) = L_ref(f) − cal_ref(f)`.
3. Response: `H(f) = L_dut(f) − L_ref_corr(f)`, then normalise so `H(1 kHz) = 0 dB`.
4. Report mean and spread (max − min) over the three runs.
5. Optional sensitivity (with calibrator 94 dB SPL @ 1 kHz on the INMP441 port):
   device `rms_dbfs` = sensitivity in this repo's dBFS convention; +3.01 dB for the
   datasheet convention.

### 3.4 Optional: log sweep

A 20 Hz – 8 kHz exponential sweep (Farina method) recorded with both mics gives a
higher-resolution curve and separates harmonic distortion. Use it only as a cross-check
of 3.2; the stepped sine table is the reference result.

### 3.5 Results

| f [Hz] | H(f) mean [dB re 1 kHz] | spread [dB] | f0 check |
| --- | --- | --- | --- |
| 63 … 6300 | TODO | TODO | TODO |

Sensitivity @ 94 dB SPL, 1 kHz: TODO dBFS (this repo's convention).

## 4. Pipeline timing (soak test)

Not acoustic, but listed in AUDIT.md as unverified.

1. Run `env:esp32dev` with WiFi for 30 min with the receiver in `--no-plot` mode.
2. From the serial log record: final `drop`, `i2s_to`, `err`, `proc_us` last/max.
3. From the receiver summary: `lost`, `late`, `gap_samples`, `restarts`.

| Metric | Value |
| --- | --- |
| Capture drops (`drop`) | TODO |
| I2S timeouts (`i2s_to`) | TODO |
| Process time per frame, max (`proc_us`) | TODO µs (budget: 32 000 µs per frame) |
| Network loss (`lost`) | TODO |
