# ESP32 + INMP441 I2S Digital Audio System

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Core%20v6.2+-orange.svg)](https://platformio.org/)
[![Platform](https://img.shields.io/badge/Platform-Espressif32-red.svg)](https://docs.platformio.org/page/boards/espressif32/esp32dev.html)
[![Framework](https://img.shields.io/badge/Framework-Arduino-blue.svg)](https://www.arduino.cc/)
[![Target](https://img.shields.io/badge/Hardware-ESP32--WROOM--32D-green.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Sensor](https://img.shields.io/badge/Microphone-INMP441%20I2S%20MEMS-lightgrey.svg)](https://invensense.tdk.com/products/digital/inmp441/)
[![License](https://img.shields.io/badge/License-MIT-brightgreen.svg)](LICENSE)

A modular, scalable, production-grade embedded firmware architecture for capturing high-fidelity digital audio on an **ESP32-WROOM-32D** development board using an **INMP441** omnidirectional I2S MEMS microphone.

Engineered with clean separation of concerns, dedicated FreeRTOS multi-core task scheduling, DMA double-buffering, real-time signal analysis (RMS, dBFS, peak magnitude), and an interactive Serial VU meter.

---

## Table of Contents

1. [Project Overview & Objectives](#1-project-overview--objectives)
2. [Hardware Wiring & Pinout Matrix](#2-hardware-wiring--pinout-matrix)
3. [Prerequisites & Arch Linux Setup Guide](#3-prerequisites--arch-linux-setup-guide)
4. [Building, Flashing & Monitoring](#4-building-flashing--monitoring)
5. [Firmware Architecture](#5-firmware-architecture)
6. [I2S Protocol & Audio Bit-Shift Mechanics](#6-i2s-protocol--audio-bit-shift-mechanics)
7. [Future Roadmap](#7-future-roadmap)
8. [Troubleshooting & Diagnostics](#8-troubleshooting--diagnostics)

---

## 1. Project Overview & Objectives

Analog electret microphones are prone to high electromagnetic interference (EMI), voltage rail ripple, and ADC non-linearities when paired with microcontrollers. The **INMP441** eliminates these drawbacks by integrating:
- An omnidirectional MEMS acoustic sensor
- An internal signal conditioning amplifier
- A 24-bit delta-sigma Analog-to-Digital Converter (ADC)
- An industry-standard **I2S (Inter-IC Sound)** digital serial bus interface

### Core Engineering Objectives
- **Zero-Jitter Ingestion**: Uses direct memory access (DMA) ring buffers managed by the ESP32 hardware I2S peripheral, bypassing CPU polling.
- **Deterministic Multi-Core Scheduling**: Pins audio sampling to **FreeRTOS Core 1**, leaving Core 0 and `loop()` completely unblocked for networking, MQTT, or DSP.
- **Strict Separation of Concerns**: Isolates hardware pin configuration, peripheral drivers, and DSP/telemetry logic into independent, unit-testable modules.
- **Plug-and-Play Extensibility**: Designed as an expandable foundation for FFT spectrum analysis, Edge AI keyword detection, and low-latency WiFi streaming.

---

## 2. Hardware Wiring & Pinout Matrix

The INMP441 operates as an I2S slave, receiving bit clock (`SCK`) and frame sync (`WS`) from the ESP32 (master), while clocking out digital PCM audio on the `SD` line.

### Pin Connection Table

| INMP441 Pin | Pin Name / Function | ESP32-WROOM-32D Pin | GPIO Designation | Electrical Details |
| :--- | :--- | :--- | :--- | :--- |
| **VDD** | Power Supply | `3V3` | Power Rail | **1.8V to 3.3V DC only**. Never connect to 5V (damages MEMS IC). |
| **GND** | Power Ground | `GND` | Ground Rail | Common reference ground. |
| **L/R** | Left/Right Channel Select | `GND` | Ground Rail | **Tied to GND selects Left Channel (Mono)**. Tied to VDD selects Right. |
| **SD** | Serial Data (Output) | `D32` | `GPIO 32` | 24-bit audio stream output from INMP441 to ESP32 RX. |
| **WS** | Word Select / LRCLK | `D15` | `GPIO 15` | Frame synchronization clock ($f_{sample}$ = 16 kHz). |
| **SCK** | Serial Clock / BCLK | `D14` | `GPIO 14` | Continuous bit clock ($f = f_{sample} \times 32 \times 2 = 1.024\text{ MHz}$). |

### Schematic Diagram

```
+-----------------------------------+             +-----------------------+
|        ESP32-WROOM-32D            |             |   INMP441 MEMS Mic    |
|                                   |             |                       |
|                             3V3   |------------>| VDD (3.3V)            |
|                             GND   |------+----->| GND                   |
|                                   |      |      |                       |
|                                   |      +----->| L/R (Channel: Left)   |
|                                   |             |                       |
|  I2S Master BCLK  (GPIO 14 / D14) |------------>| SCK (Serial Clock)    |
|  I2S Master LRCLK (GPIO 15 / D15) |------------>| WS  (Word Select)     |
|  I2S Master RX    (GPIO 32 / D32) |<------------| SD  (Serial Data)     |
+-----------------------------------+             +-----------------------+
```

> [!TIP]
> For in-depth electrical layout guidelines, decoupling capacitor placement, and signal integrity notes, see [`docs/hardware/wiring.md`](docs/hardware/wiring.md).

---

## 3. Prerequisites & Arch Linux Setup Guide

This project is fully compatible with any modern Linux distribution, with streamlined setup instructions tailored for **Arch Linux / Manjaro / EndeavourOS**.

### 3.1 Install PlatformIO Core

PlatformIO can be installed directly from the Arch Linux official repositories or through Python `venv` / `pipx`:

```bash
# Option A: System package via pacman (Arch Linux official repo)
sudo pacman -Syu platformio-core

# Option B: Isolated installation via pipx (recommended if managing multiple Python versions)
sudo pacman -S python-pipx
pipx install platformio
pipx ensurepath
```

Verify your installation:
```bash
pio --version
# Expected output: PlatformIO Core, version 6.x.x
```

### 3.2 Configure Arch Linux `udev` Rules & Serial Permissions

By default, Linux disallows non-root users from accessing USB serial transceivers (`/dev/ttyUSB0` or `/dev/ttyACM0`).

1. **Install PlatformIO udev rules**:
   You can install the official rules package from the AUR or place the file directly:
   ```bash
   # Download official PlatformIO udev rules
   sudo curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
       -o /etc/udev/rules.d/99-platformio-udev.rules

   # Reload and trigger udev rules
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

2. **Add your user to serial device groups**:
   On Arch Linux, serial ports belong to the `uucp` group (and additionally `lock` or `dialout`):
   ```bash
   sudo usermod -aG uucp $USER
   sudo usermod -aG dialout $USER
   ```

3. **Apply group changes**:
   Log out and log back in, or activate the group in your current subshell:
   ```bash
   newgrp uucp
   ```

---

## 4. Building, Flashing & Monitoring

Connect your ESP32 board to your computer via USB.

### 4.1 Compile the Project
```bash
pio run
```

### 4.2 Flash the Firmware
PlatformIO will auto-detect the serial port (e.g., `/dev/ttyUSB0`):
```bash
pio run -t upload
```
*To explicitly specify a port:*
```bash
pio run -t upload --upload-port /dev/ttyUSB0
```

### 4.3 Open the Serial Telemetry Monitor
```bash
pio run -t monitor
```

### 4.4 Build, Upload, and Monitor in One Command
```bash
pio run -t upload -t monitor
```

### Expected Serial Monitor Output
Once booted, the firmware outputs system metadata followed by real-time VU meter telemetry:

```text
==================================================
   ESP32-WROOM-32D + INMP441 I2S Audio System     
==================================================
Sample Rate : 16000 Hz
DMA Buffers : 4 x 256 samples
Pinout      : SCK=14, WS=15, SD=32
--------------------------------------------------
[OK] I2S Driver initialized successfully.
[OK] Audio FreeRTOS task spawned on Core 1.
Streaming real-time VU telemetry to Serial Monitor...

[Task] Audio acquisition task running on Core 1
VU: [=>                            ] | Peak: -42.10 dBFS | RMS: 0.008 | MaxMag:   67104
VU: [====>                         ] | Peak: -31.45 dBFS | RMS: 0.027 | MaxMag:  225810
VU: [===============>              ] | Peak: -14.20 dBFS | RMS: 0.195 | MaxMag: 1634892
VU: [=========================>    ] | Peak:  -3.80 dBFS | RMS: 0.645 | MaxMag: 5410880
```

Speak or clap near the microphone to watch the ASCII VU meter dynamically react!

---

## 5. Firmware Architecture

The codebase follows an enterprise embedded C++ structure designed for modular expansion:

```
esp32-inmp441-mic/
├── docs/
│   └── hardware/
│       └── wiring.md             # Detailed pinout, electrical characteristics, schematics
├── include/
│   ├── config.h                  # Central hardware pin definitions, sample rates, buffer settings
│   ├── audio_input.h             # Hardware I2S driver interface & DMA abstraction
│   └── audio_processor.h         # DSP metrics, RMS/dBFS math, and VU meter visualizer
├── src/
│   ├── audio_input.cpp           # Concrete ESP-IDF I2S DMA implementation
│   ├── audio_processor.cpp       # Signal conditioning and audio calculations
│   └── main.cpp                  # FreeRTOS Core 1 task lifecycle and serial telemetry
├── lib/                          # Directory for project-specific external libraries
├── platformio.ini                # Build configuration, upload flags, and monitor filters
└── README.md                     # Main documentation
```

### Module Responsibilities

1. **[`include/config.h`](include/config.h)**:
   - Single source of truth for all configurable constants.
   - Modifying pins, sample rates (e.g. 16 kHz to 44.1 kHz), DMA buffer depths, or task priorities is done purely within this header without touching driver logic.

2. **[`include/audio_input.h`](include/audio_input.h) / [`src/audio_input.cpp`](src/audio_input.cpp)**:
   - Wraps ESP-IDF's robust native I2S DMA driver.
   - Manages peripheral initialization (`i2s_driver_install`, `i2s_set_pin`), DMA zeroing, pause/resume, and zero-copy block reads.

3. **[`include/audio_processor.h`](include/audio_processor.h) / [`src/audio_processor.cpp`](src/audio_processor.cpp)**:
   - Encapsulates signal math: 24-bit MSB sample extraction, running sum-of-squares calculation for true RMS, peak detection, and conversion to logarithmic Decibels Full Scale (dBFS).
   - Provides an ASCII VU meter renderer for real-time serial diagnostics.

4. **[`src/main.cpp`](src/main.cpp)**:
   - Spawns the dedicated `audioTask` on **Core 1** with priority 5 (`xTaskCreatePinnedToCore`).
   - Keeps `setup()` and `loop()` decoupled from audio acquisition timing.

---

## 6. I2S Protocol & Audio Bit-Shift Mechanics

### Frame Structure
The INMP441 transmits 24-bit 2's complement audio words inside standard 32-bit I2S subframes.

```
+---------------------------------- 32-Bit Frame ----------------------------------+
| Bit 31 (MSB) ................. Bit 8 | Bit 7 ......................... Bit 0 (LSB) |
|            24-Bit Audio Data         |           8 Unused Zero Bits              |
+--------------------------------------+-------------------------------------------+
```

### Scaling Math in Firmware
When the ESP32 I2S peripheral reads a 32-bit slot via DMA into an `int32_t`:
1. The 24 active audio bits occupy the most significant bits `[31:8]`.
2. To extract the true signed 24-bit sample:
   ```cpp
   int32_t sample24 = raw_sample >> 8;
   ```
3. To normalize the sample into a floating-point value between $-1.0$ and $+1.0$:
   ```cpp
   float normalized = static_cast<float>(sample24) / 8388607.0f; // 2^23 - 1
   ```
4. Root Mean Square (RMS) power across $N$ samples:
   $$\text{RMS} = \sqrt{\frac{1}{N}\sum_{i=1}^{N} \text{normalized}_i^2}$$
5. Decibels relative to Full Scale:
   $$\text{dBFS} = 20 \log_{10}(\text{RMS})$$

---

## 7. Future Roadmap

- [x] **Phase 1: Foundation**
  - Modular I2S driver architecture with FreeRTOS multi-core task pinning.
  - Signal conditioning, RMS, peak calculation, and ASCII VU visualizer.
  - Comprehensive documentation and Arch Linux setup guides.

- [ ] **Phase 2: On-Device DSP & Fast Fourier Transform (FFT)**
  - Integrate `arduinoFFT` or ESP-DSP hardware-accelerated FFT routines.
  - Real-time 16/32-band frequency spectrum visualizer over Serial.
  - Configurable IIR/FIR high-pass filter to remove DC offset and low-frequency handling rumble (< 80 Hz).

- [ ] **Phase 3: Wireless Audio Streaming**
  - WiFi AP / Station connectivity.
  - Real-time low-latency audio transmission over **UDP Multicast / RTP**.
  - WebSocket PCM stream server for browser-based real-time oscilloscopes.

- [ ] **Phase 4: Edge AI & Voice Recognition**
  - Voice Activity Detection (VAD) algorithm for low-power sleep wake-up.
  - TensorFlow Lite for Microcontrollers (TFLM) keyword spotting model (e.g., "Hey ESP").

- [ ] **Phase 5: Stereo Acoustic Array**
  - Connect a second INMP441 with `L/R` tied to `3V3` (Right Channel).
  - Stereo beamforming and acoustic localization (Time Difference of Arrival - TDOA).

---

## 8. Troubleshooting & Diagnostics

### 1. `Permission denied: '/dev/ttyUSB0'`
- **Cause**: User account missing serial group permissions.
- **Solution**: Execute `sudo usermod -aG uucp $USER` (Arch Linux) or `sudo usermod -aG dialout $USER` (Ubuntu/Debian), then log out and back in.

### 2. Flatline Audio Output (All Zeros or Constant Low Noise)
- **Check `L/R` Pin**: Ensure the `L/R` pin is securely tied to `GND`. If left floating, the microphone behavior is undefined.
- **Check Power Rail**: Ensure `VDD` is connected to `3V3`, not `5V` or an unpowered rail.
- **Inspect Pin Mapping**: Verify that GPIO 14 (SCK), GPIO 15 (WS), and GPIO 32 (SD) correspond to your board's physical pin layout.

### 3. ESP32 Fails to Flash / Reset Issues
- **Cause**: `GPIO 15` is a strapping pin (`MTDO`). Some ESP32 development boards require this pin to be in a specific state during reset.
- **Solution**: Hold down the **BOOT** button while PlatformIO initiates the upload sequence. Alternatively, if flashing conflicts persist, remap `PIN_I2S_WS` to `GPIO 25` in [`include/config.h`](include/config.h).

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.