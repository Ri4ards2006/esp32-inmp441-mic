# ESP32 + INMP441 audio pipeline — portfolio summary

[English](#english) · [Deutsch](#deutsch)

---

## English

### What it is
Embedded firmware that turns an ESP32 and a €2 INMP441 MEMS microphone into a small
network audio sensor: 24-bit I2S capture, on-device signal analysis (level, dominant
frequency, voice activity) and a UDP stream to a PC tool with a live spectrogram and
WAV recording.

### Engineering problems solved
- **Correct sample format.** The INMP441 sends 24-bit two's complement in a 32-bit I2S
  slot. The original code shifted twice in one path (levels 48 dB too low) and scaled
  asymmetrically. Unpacking, scaling (2²³) and a one-pole DC blocker now live in a
  documented, unit-tested header.
- **Real-time without blocking.** Capture, processing and network transport run as
  three FreeRTOS tasks on two cores. Frames move by pointer through lock-free
  single-producer/single-consumer rings from a fixed pool: no heap allocation in the
  audio path, no locks, no stage waiting on another. Under network back-pressure the
  system drops and *counts* whole frames instead of overrunning the I2S DMA.
- **DSP on a microcontroller.** 512-point Hann-windowed FFT via Espressif's esp-dsp,
  peak frequency refined by log-parabolic interpolation, energy VAD with an adaptive
  noise floor and hangover.
- **Firmware/host contract.** The UDP format is specified once and checked by the same
  golden byte vector in the C++ and the Python test suites.
- **Build hygiene.** Found that the umbrella `esp_dsp.h` include pulled `<iostream>`
  and ~240 KB of libstdc++ into flash; narrowing the includes brought the image back
  to 23 % of flash. Default build no longer depends on a gitignored 29 MB SDK, so a
  clean clone and CI build.

### Verification
- 33 C++ host tests (Unity, `pio test -e native`): sample conversion, DC blocker,
  SPSC ring incl. a two-thread stress test, levels, FFT peak against a reference DFT,
  VAD behaviour, packet encoding.
- 12 Python tests incl. a UDP loopback that writes and checks a WAV file.
- GitHub Actions: firmware build (with and without WiFi), host tests, receiver tests.
- Acoustic measurements (noise floor, frequency response) and on-hardware timing are
  **open**: the procedure is written ([measurement.md](measurement.md)), results are TODO.

### Stack
C++17, ESP32 (Arduino core 2.0.17 / ESP-IDF 4.4), FreeRTOS, I2S DMA, esp-dsp,
PlatformIO, Unity, Python (numpy, matplotlib), uv, GitHub Actions.

---

## Deutsch

### Worum es geht
Embedded-Firmware, die einen ESP32 und ein INMP441-MEMS-Mikrofon (ca. 2 €) zu einem
kleinen Netzwerk-Audiosensor macht: 24-Bit-I2S-Erfassung, Signalanalyse auf dem Gerät
(Pegel, dominante Frequenz, Sprachaktivität) und ein UDP-Stream zu einem PC-Tool mit
Live-Spektrogramm und WAV-Aufnahme.

### Gelöste technische Probleme
- **Korrektes Sampleformat.** Das INMP441 liefert 24 Bit Zweierkomplement in einem
  32-Bit-I2S-Slot. Der ursprüngliche Code schob in einem Pfad doppelt (Pegel 48 dB zu
  niedrig) und skalierte asymmetrisch. Entpacken, Skalierung (2²³) und ein
  einpoliger DC-Blocker liegen jetzt in einem dokumentierten, getesteten Header.
- **Echtzeit ohne Blockieren.** Erfassung, Verarbeitung und Netzwerktransport laufen
  als drei FreeRTOS-Tasks auf zwei Kernen. Frames wandern als Zeiger durch lock-freie
  Single-Producer/Single-Consumer-Ringe aus einem festen Pool: keine Heap-Allokation im
  Audiopfad, keine Locks, keine Stufe wartet auf eine andere. Bei Netzwerk-Rückstau
  verwirft das System ganze Frames und *zählt* sie, statt den I2S-DMA überlaufen zu lassen.
- **DSP auf dem Mikrocontroller.** 512-Punkt-FFT mit Hann-Fenster über Espressifs
  esp-dsp, Spitzenfrequenz per log-parabolischer Interpolation verfeinert,
  Energie-VAD mit adaptivem Rauschboden und Nachlaufzeit.
- **Vertrag Firmware/Host.** Das UDP-Format ist einmal spezifiziert und wird in den
  C++- und Python-Tests mit demselben Golden-Byte-Vektor geprüft.
- **Build-Hygiene.** Der Sammel-Include `esp_dsp.h` zog `<iostream>` und ~240 KB
  libstdc++ in den Flash; mit gezielten Includes liegt das Image wieder bei 23 % Flash.
  Der Standard-Build hängt nicht mehr von einem gitignorierten 29-MB-SDK ab, ein
  frischer Clone und die CI bauen.

### Verifikation
- 33 C++-Host-Tests (Unity, `pio test -e native`): Sample-Konvertierung, DC-Blocker,
  SPSC-Ring inkl. Zwei-Thread-Stresstest, Pegel, FFT-Peak gegen Referenz-DFT,
  VAD-Verhalten, Paket-Kodierung.
- 12 Python-Tests inkl. UDP-Loopback, der eine WAV-Datei schreibt und prüft.
- GitHub Actions: Firmware-Build (mit und ohne WLAN), Host-Tests, Receiver-Tests.
- Akustische Messungen (Rauschboden, Frequenzgang) und Timing auf echter Hardware sind
  **offen**: Das Verfahren ist beschrieben ([measurement.md](measurement.md)), die
  Ergebnisse sind TODO.

### Technologien
C++17, ESP32 (Arduino-Core 2.0.17 / ESP-IDF 4.4), FreeRTOS, I2S-DMA, esp-dsp,
PlatformIO, Unity, Python (numpy, matplotlib), uv, GitHub Actions.
