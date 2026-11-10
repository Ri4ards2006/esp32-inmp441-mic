# HANDOFF — branch `claude/audio-pipeline` (2026-11-10)

## Done (one commit each)
0. `AUDIT.md` — toolchain, I2S config as found, findings F1–F11.
1. I2S fix: `slot >> 8`, scaling by 2²³, one-pole DC blocker (20 Hz), slot selectable (`MIC_I2S_SLOT`); double shift in old AudioProcessor removed.
2. FreeRTOS pipeline capture → process → transport: 8-frame static pool, lock-free SPSC rings, drop counters, 1 Hz serial stats. Env split: `esp32dev` (pipeline, 16 kHz) / `esp32dev-kws` (old keyword spotter).
3. DSP: RMS/peak dBFS, 512-pt esp-dsp FFT peak (interpolated), energy VAD.
4. UDP transport "MIC1" (WiFi from gitignored `include/secrets.h`) + `tools/receiver/mic_receiver.py` (live spectrogram, WAV, `--simulate`).
5. `docs/measurement.md` — procedure only, results TODO.
6. CI (`.github/workflows/ci.yml`), platform pinned to espressif32@6.12.0, README (wiring + Mermaid), `docs/PORTFOLIO.md` (EN/DE).

Verified locally: `pio run -e esp32dev` (with/without secrets) and `-e esp32dev-kws` build; 33 C++ host tests + 12 pytest green; clean clone builds; simulator → receiver → WAV end-to-end.

## Not done
- **Nothing tested on hardware** (no board attached). I2S slot order, levels, timing (`proc_us`, drops) and WiFi throughput are unverified.
- No measurements (all TODO in `docs/measurement.md`).
- KWS path findings F2 (decimation w/o anti-alias), F3 (serial raw stream exceeds 115200 baud), F8 (millis wrap) documented, not fixed.
- CI has not run yet on GitHub (first run comes with the PR).

## Open questions for Richard
1. `.vscode/c_cpp_properties.json` and `launch.json` have local uncommitted changes (yours, pre-existing) and are tracked despite `.gitignore` — untrack them (`git rm --cached`)? I left them untouched.
2. Is 16 kHz OK as default for the pipeline, or should it match the KWS rate (15.36 kHz)?
3. Should the KWS firmware move onto the new pipeline (process stage → inference) with a proper decimation filter?

## Next 3 steps
1. Flash `pio run -e esp32dev -t upload`, check serial: `[pipe] drop=0 i2s_to=0`, `[dsp] rms` reacts to speech; if silent, try `-DMIC_I2S_SLOT=1`.
2. Create `include/secrets.h`, run `uv run tools/receiver/mic_receiver.py --wav test.wav`, verify spectrogram + `lost=0`; then 30-min soak (measurement.md §4).
3. Run noise-floor and frequency-response procedure (measurement.md §2–3), fill in the TODO tables, tune VAD thresholds in `config.h`.
