# AUDIT — state of `main` @ `67c6dff` (2026-11-10)

Baseline audit before the `claude/audio-pipeline` work. Everything below was read
from the repository or produced by a local build. **No hardware was attached during
this audit** (`/dev/ttyUSB*` / `/dev/ttyACM*` empty), so nothing here is a measured
acoustic or timing value.

## 1. Toolchain

| Item | Found |
| --- | --- |
| Build system | PlatformIO Core 6.2.0 (local) |
| Platform | `espressif32` — **unpinned** in `platformio.ini`; resolves to 6.12.0 locally |
| Framework | Arduino-ESP32 core 2.0.17 (package `3.20017.241212`), i.e. ESP-IDF 4.4.x |
| I2S API | legacy `driver/i2s.h` (`i2s_driver_install` / `i2s_read`) |
| Board | `esp32dev` (ESP32-WROOM-32D per README) |
| Flags | `-O3`, `CORE_DEBUG_LEVEL=0`, include paths into `lib/edge-impulse-sdk` |
| DSP library | `esp-dsp` ships precompiled inside the Arduino core (`libespressif__esp-dsp.a`) — not used yet |
| Tests | `test/` is empty, no CI |

Local build of `main`: `pio run -e esp32dev` → **SUCCESS** in ~63 s,
RAM 9.8 % (32 048 B), Flash 28.2 % (369 257 B).

## 2. I2S configuration as found (`include/config.h`, `src/audio_input.cpp`)

| Parameter | Value |
| --- | --- |
| Port / role | `I2S_NUM_0`, master, RX |
| Sample rate | 15 360 Hz (chosen so that ÷4 = 3 840 Hz for the Edge Impulse model) |
| Bit depth / slot | `I2S_BITS_PER_SAMPLE_32BIT` (32-bit slot, INMP441 delivers 24 bit) |
| Channel format | `I2S_CHANNEL_FMT_RIGHT_LEFT` (stereo), left slot taken in software (index `2*i`) |
| Comm format | `I2S_COMM_FORMAT_STAND_I2S` (Philips, 1-bit delay) |
| BCLK | 15 360 × 32 × 2 = 983 040 Hz (derived, not measured) |
| DMA | 8 buffers × 256 frames; APLL off |
| Pins | SCK/BCLK = GPIO 14, WS/LRCLK = GPIO 15, SD = GPIO 32, status LED = GPIO 2 |
| L/R strap | documented as tied to GND (left channel) |

Sample conversion: `slot >> 8` → signed 24 bit, then `>> 6` → int16 (i.e. +12 dB
relative to a plain `>> 8`), clipped.

## 3. What works (per repo history and docs — not re-verified on hardware)

- I2S capture of the INMP441 left slot (commit `b36a02c` "verified stereo dual-slot").
- Edge Impulse keyword spotter for "hello", LED on GPIO 2 (commits `15480fb`, `67c6dff`).
- Energy gate before inference (peak < 150 LSB → skip NN).
- Raw CSV serial stream mode for `edge-impulse-data-forwarder` (`STREAM_RAW_SAMPLES`).
- Stand-alone diagnostic firmware on branch `fix/mic-test`.
- Firmware compiles (verified locally, see §1).

## 4. Findings

| # | Severity | Finding |
| --- | --- | --- |
| F1 | High | **Clean clones cannot build.** `lib/edge-impulse-sdk/*` is gitignored (29 MB local only, just `library.json` tracked) while `src/main.cpp` includes `ei_run_classifier.h`. Any CI would fail. |
| F2 | High | **Decimation without anti-alias filter.** `main.cpp` keeps every 4th sample (15 360 → 3 840 Hz). Content between 1.92 kHz and 7.68 kHz folds into the 0–1.92 kHz band. |
| F3 | High | **Raw stream cannot keep up.** `STREAM_RAW_SAMPLES` prints one `println` per sample at 3 840 Hz. Even a 2-digit value (`"12\r\n"`, 4 B) needs 15 360 B/s; 115 200 baud carries ≈ 11 520 B/s. `Serial.println` blocks → I2S DMA overrun (computed, not measured). |
| F4 | Medium | **Double shift in `AudioProcessor::analyze`.** `AudioInput::read` already returns `slot >> 8`; `analyze()` shifts `>> 8` again → levels 48 dB too low. Not called from `main.cpp` today, but latent. |
| F5 | Medium | **No DC offset removal.** RMS/dBFS and the energy gate include any DC component of the mic/ADC path. |
| F6 | Medium | **Acquisition and inference share one task.** While `run_classifier` runs, nobody drains I2S. DMA headroom = 8 × 256 / 15 360 Hz ≈ 133 ms. If DSP+NN exceed that, frames are dropped silently — there is no overrun counter. |
| F7 | Low | Normalisation by 8 388 607 instead of 2²³ (asymmetric; −8 388 608 maps below −1.0). |
| F8 | Low | `led_turn_off_time` uses `millis() >= t`, breaks at the 49.7-day wrap. |
| F9 | Low | `platform = espressif32` unpinned → non-reproducible builds. |
| F10 | Low | `.vscode/c_cpp_properties.json` and `launch.json` are tracked although listed in `.gitignore` (they show up as local modifications). |
| F11 | Info | Docs contain unmeasured claims ("zero frame-dropping", "jitter-free"); `docs/hardware/wiring.md` uses 16 kHz / 1.024 MHz examples, firmware runs 15.36 kHz. |

## 5. Scope of the follow-up work

Addressed on `claude/audio-pipeline`: F1 (pipeline firmware builds without the EI
SDK; KWS moved to an opt-in env), F4/F5/F7 (new conversion + DC blocker), F6 (separate
capture/process/transport tasks with drop counters), F9 (platform pinned), new CI.
F2/F3/F8 live in the legacy KWS / raw-stream path and are documented, not rewritten.
F10 is left to the owner (would touch local IDE files).
