# INMP441 I2S Hardware Diagnostic & Troubleshooting Guide

This guide describes how to run the isolated hardware diagnostic test suite on branch `fix/mic-test` to determine why audio data might register as flatline zeros.

---

## 1. Overview of the Diagnostic Firmware

The diagnostic firmware in [`src/main.cpp`](../../src/main.cpp) bypasses all abstractions and directly configures the ESP32 hardware I2S peripheral (`I2S_NUM_0`) with the following diagnostic features:

1. **Dual-Slot Stereo Acquisition (`I2S_CHANNEL_FMT_RIGHT_LEFT`)**:
   - Simultaneously samples both the **Left channel** (L/R tied to GND) and **Right channel** (L/R tied to 3V3).
   - This bypasses the known ESP32 mono channel-swapping silicon quirk where `ONLY_LEFT` can sample the inactive slot.
2. **Real-Time Signal Metrics**:
   - Computes independent statistics for each channel:
     - **Non-Zero Sample Percentage** (`%`): Detects if any signal is arriving over DMA.
     - **Peak-to-Peak Amplitude** (`max - min`): Measures acoustic dynamic range.
     - **RMS Power**: Measures effective sound pressure level.
     - **Raw Sample Hex Dump**: Displays the 32-bit slot word to verify 24-bit MSB alignment.
3. **Dual ASCII VU Meter**:
   - Visual real-time bar graphs for both Left and Right channels updated at 10 Hz.

---

## 2. Pinout Reference

| INMP441 Pin | ESP32-WROOM-32D Pin | GPIO | Function | Diagnostic Voltage / Level |
| :--- | :--- | :--- | :--- | :--- |
| **VDD** | 3V3 | - | Power Supply | Must measure **3.2V to 3.3V DC** against GND. |
| **GND** | GND | - | Ground Reference | Common ground with ESP32. |
| **L/R** | GND (or 3V3) | - | Channel Select | **GND = Left Channel**; **3V3 = Right Channel**. Never leave floating! |
| **SD** | D32 | GPIO 32 | I2S Data Output | Toggles with audio bitstream. |
| **WS** | D15 | GPIO 15 | Word Select (LRCLK) | 16 kHz square wave (1.65V average on DC multimeter). |
| **SCK** | D14 | GPIO 14 | Bit Clock (BCLK) | 1.024 MHz clock (1.65V average on DC multimeter). |

---

## 3. How to Run the Test

### Step 1: Free the Serial Port
If `edge-impulse-data-forwarder` or another serial monitor is running in a terminal, terminate it with `Ctrl+C`.

### Step 2: Upload and Monitor
```bash
pio run -t upload -t monitor
```

### Step 3: Interpret the Output

#### Case A: Normal Operation (Microphone Functional)
When you speak, blow, or tap the microphone:
```text
L: [======>           ]  R: [                  ] | L_RMS: 0.124 P2P:  185420 | R_RMS: 0.000 P2P:       0

---------------------- [INMP441 DIAGNOSTIC STATUS] ----------------------
Left Channel  (L/R->GND): Non-Zero:  99.8% | P2P:   210450 | RMS: 0.145 | Raw[0]: 0x021A3400
Right Channel (L/R->3V3): Non-Zero:   0.0% | P2P:        0 | RMS: 0.000 | Raw[0]: 0x00000000
>>> RESULT: [PASS] ACTIVE AUDIO DETECTED ON LEFT CHANNEL (L/R -> GND).
-------------------------------------------------------------------------
```
- The VU meter reacts dynamically.
- `P2P` registers values > 10,000 when speaking.

#### Case B: Channel Swap (L/R tied to 3.3V or inverted)
If the audio appears on the **Right Channel** instead of Left:
```text
>>> RESULT: [PASS] ACTIVE AUDIO DETECTED ON RIGHT CHANNEL (L/R -> 3V3).
```
- This indicates the microphone's `L/R` pin is connected to `3V3` instead of `GND`. Move `L/R` to `GND` for standard left-channel mono.

#### Case C: Flatline (All Zeros)
```text
L: [                  ]  R: [                  ] | L_RMS: 0.000 P2P:       0 | R_RMS: 0.000 P2P:       0

---------------------- [INMP441 DIAGNOSTIC STATUS] ----------------------
Left Channel  (L/R->GND): Non-Zero:   0.0% | P2P:        0 | RMS: 0.000 | Raw[0]: 0x00000000
Right Channel (L/R->3V3): Non-Zero:   0.0% | P2P:        0 | RMS: 0.000 | Raw[0]: 0x00000000
>>> RESULT: [FAIL] FLATLINE DETECTED (ALL ZEROS ON BOTH CHANNELS)!
-------------------------------------------------------------------------
```
If you see this:
1. **Multimeter Check**: Measure DC voltage between `VDD` and `GND` on the INMP441 board pins directly. It must be **3.3V**.
2. **Check `L/R` Pin**: If `L/R` is floating or has a poor connection, the chip may stay in high-impedance mode. Tie it directly to `GND`.
3. **Inspect Solder Joints**: INMP441 boards often come with unsoldered pin headers. Header pins that are loosely resting in through-holes without solder will not establish contact for high-frequency clock signals (>1 MHz).
4. **Jumper Wires**: Swap breadboard jumper wires on SCK (GPIO 14), WS (GPIO 15), and SD (GPIO 32).
