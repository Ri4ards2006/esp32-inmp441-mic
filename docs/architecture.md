# ESP32 + INMP441 Audio Subsystem Architecture

This document specifies the internal firmware architecture, hardware peripheral routing, DMA memory topology, and digital signal processing pipeline for the **ESP32-WROOM-32D** interfacing with the **InvenSense INMP441** digital MEMS microphone.

---

## 1. System Topology & Data Flow

The audio acquisition engine is organized into four distinct architectural layers with strict separation between hardware peripheral control, signal conditioning, and serialization.

```
+---------------------------------------------------------------------------------------+
|                                    HARDWARE LAYER                                     |
|  +--------------------+        I2S Bus (SCK: 14, WS: 15, SD: 32)   +---------------+  |
|  |  INMP441 MEMS Mic  | =========================================> | ESP32 I2S0 HW |  |
|  | (L/R tied to GND)  |     (983.04 kHz BCLK, 15.36 kHz LRCLK)     |  Peripheral   |  |
|  +--------------------+                                            +-------+-------+  |
+----------------------------------------------------------------------------|----------+
                                                                             | DMA
+----------------------------------------------------------------------------v----------+
|                                 DIRECT MEMORY ACCESS (DMA)                            |
|       +------------------------------------------------------------------------+      |
|       |  Descriptor Ring Buffer: 4 buffers x 256 samples (Stereo 32-bit pairs) |      |
|       |  Total Capacity: 1024 frames (66.67 ms jitter absorption window)        |      |
|       +-----------------------------------+------------------------------------+      |
+-------------------------------------------|-------------------------------------------+
                                            | i2s_read()
+-------------------------------------------v-------------------------------------------+
|                               FIRMWARE & DRIVER LAYER                                 |
|  +---------------------------------------------------------------------------------+  |
|  | AudioInput Class (Core 1 FreeRTOS Task)                                         |  |
|  |  1. De-interleave Stereo: Extract Left Channel (subframe index 2*i)             |  |
|  |  2. Unpack 24-bit MSB-justified PCM from 32-bit slot container                 |  |
|  +----------------------------------------+----------------------------------------+  |
+-------------------------------------------|-------------------------------------------+
                                            |
+-------------------------------------------v-------------------------------------------+
|                         SIGNAL CONDITIONING & DECIMATION                              |
|  +---------------------------------------------------------------------------------+  |
|  | Mathematical Decimation (M = 4):                                                |  |
|  |   15,360 Hz raw input  ===>  3,840 Hz Edge Impulse output stream                 |  |
|  | Sensitivity Boost (+12 dB / 4x gain):                                           |  |
|  |   Saturated clamp to signed 16-bit integer [-32768, 32767]                      |  |
|  +----------------------------------------+----------------------------------------+  |
+-------------------------------------------|-------------------------------------------+
                                            |
+-------------------------------------------v-------------------------------------------+
|                                SERIALIZATION & INGESTION                              |
|  +---------------------------------------------------------------------------------+  |
|  | UART0 @ 115200 baud -> edge-impulse-data-forwarder -> Edge Impulse Studio      |  |
|  | Format: Single ASCII integer per line (-1540\r\n)                               |  |
|  +---------------------------------------------------------------------------------+  |
+---------------------------------------------------------------------------------------+
```

---

## 2. Hardware Peripheral & Clock Tree Invariants

### 2.1 The INMP441 Clocking Constraints
The INMP441 is an I2S slave device containing an integrated acoustic sensor, pre-amplifier, fourth-order sigma-delta modulator, and digital decimation filter.

According to the **InvenSense INMP441 Datasheet (Section 4.1)**:
- **Bit Clock Frequency ($f_{\text{SCK}}$)**: Must remain strictly between **$600\text{ kHz}$ and $3.3\text{ MHz}$**.
- If $f_{\text{SCK}} < 600\text{ kHz}$, the internal analog charge pumps lose regulation, causing the microphone to power down or emit static noise.

### 2.2 Mathematical Clock Synthesis
The ESP32 I2S hardware clock generator derives BCLK and LRCLK via an internal fractional PLL clock divider:

$$f_{\text{BCLK}} = f_{\text{sample}} \times \text{Bits per Slot} \times \text{Number of Channels}$$

With 32-bit slot width and stereo framing (2 channels):

$$f_{\text{BCLK}} = f_{\text{sample}} \times 32 \times 2 = f_{\text{sample}} \times 64$$

To obtain a downstream sampling frequency that integrates seamlessly with Edge Impulse ML models, we set:
- **Hardware Sample Rate ($f_{\text{sample}}$)**: $15,360\text{ Hz}$
- **Continuous Bit Clock ($f_{\text{BCLK}}$)**:
  $$15,360 \times 64 = 983,040\text{ Hz} = 983.04\text{ kHz}$$

This clock rate is well within the $600\text{ kHz} \dots 3.3\text{ MHz}$ envelope ($\approx 1\text{ MHz}$), guaranteeing stable microphone operation.

---

## 3. Resolving the ESP32 I2S Mono Channel-Swap Silicon Bug

### 3.1 The Silicon Quirk
The ESP32 (ESP32-D0WD-V3, Xtensa dual-core) I2S hardware peripheral contains a known silicon nuance in its master receive FIFO logic:
- When initialized in mono mode (`I2S_CHANNEL_FMT_ONLY_LEFT`), the hardware FIFO clocking may sample during the `WS = High` (Right channel) subframe window depending on the exact ESP-IDF / Arduino core revision.
- Because the INMP441 has its `L/R` pin tied to **GND**, it asserts its data line exclusively during `WS = Low` (Left channel). During `WS = High`, the INMP441 tri-states (`Hi-Z`) its `SD` pin, pulling to zero.
- Consequently, mono capture drivers frequently ingest **100% flatline zero values**.

### 3.2 The Architectural Solution: Stereo Dual-Slot Ingestion
To eliminate silicon and framework variance, our driver configures the peripheral in **Stereo Dual-Slot Mode**:
```cpp
.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT
```
1. The ESP32 DMA controller ingests interleaved 32-bit pairs:
   $$\text{Buffer} = [\text{Left}_0, \text{Right}_0, \text{Left}_1, \text{Right}_1, \dots, \text{Left}_{N-1}, \text{Right}_{N-1}]$$
2. In software, the driver de-interleaves the stream deterministically:
   ```cpp
   for (size_t i = 0; i < pairs_read; ++i) {
       // Left channel (L/R -> GND) resides at index 2*i
       buffer[i] = stereo_buf[2 * i] >> 8;
   }
   ```
3. This guarantees $100\%$ reliable Left-channel acquisition across all ESP32 silicon steppings, clock rates, and IDF versions.

---

## 4. DMA Ring Buffer & Latency Analysis

### 4.1 DMA Configuration
- **`dma_buf_count`**: $4$
- **`dma_buf_len`**: $256$ samples (interleaved pairs)
- **Sample Rate**: $15,360\text{ Hz}$

### 4.2 Latency Invariants
The time required to fill a single DMA buffer is:

$$t_{\text{buf}} = \frac{256}{15,360} \approx 16.67\text{ ms}$$

Total DMA hardware latency across all 4 ring buffers:

$$t_{\text{total}} = 4 \times 16.67\text{ ms} = 66.67\text{ ms}$$

This $66.67\text{ ms}$ cushion guarantees that temporary CPU latency spikes (e.g., high-priority WiFi interrupts or UART FIFO flushes) never cause DMA buffer under-runs or dropped audio frames.

### 4.3 Memory Footprint
To eliminate dynamic memory fragmentation (`malloc`/`free` thrashing during audio loops), the driver uses a single pre-allocated heap buffer:
$$\text{Memory} = 256 \times 2 \times 4\text{ bytes} = 2,048\text{ bytes (2 KB)}$$
Allocated once during `AudioInput::begin()` and freed on `AudioInput::end()`.

---

## 5. Decimation Math & Sensitivity Scaling

### 5.1 Decimation Formulation
The downstream Edge Impulse ML pipeline operates at **$3,840\text{ Hz}$**.

The continuous audio stream is downsampled using an integer decimation factor $M = 4$:

$$f_{\text{out}} = \frac{f_{\text{in}}}{M} = \frac{15,360\text{ Hz}}{4} = 3,840\text{ Hz}$$

### 5.2 Nyquist Envelope & Audio Bandwidth
At $f_{\text{out}} = 3,840\text{ Hz}$, the accessible acoustic bandwidth (Nyquist frequency) is:

$$f_{\text{Nyquist}} = \frac{3,840}{2} = 1,920\text{ Hz}$$

This $1.92\text{ kHz}$ acoustic window preserves:
- The fundamental human voice frequency ($F0$: $85\text{ Hz} \dots 255\text{ Hz}$)
- First formant frequencies ($F1$: $300\text{ Hz} \dots 900\text{ Hz}$)
- Second formant frequencies ($F2$: $900\text{ Hz} \dots 1,900\text{ Hz}$)
- Mechanical anomalies, motor bearing noise, glass breakage, and acoustic alarms.

### 5.3 Acoustic Sensitivity Gain
- The INMP441 sensitivity is $-26\text{ dBFS}$ at $94\text{ dB SPL}$ ($1\text{ Pa}$).
- Standard human speech at 1 meter distance produces $\approx 60\text{ dB SPL}$ ($-60\text{ dBFS}$ relative to full scale).
- Standard truncation ($\gg 8$) leaves voice signals occupying only the lowest 10-12 bits of a 16-bit word, reducing classifier SNR.
- Shifting by **$6$ bits** ($\gg 6$) instead of $8$ imparts a **$+12\text{ dB}$ ($4\times$) digital gain boost**:

$$x_{\text{16-bit}}[n] = \text{clip}\left(\frac{x_{\text{24-bit}}[n]}{2^6}, -32768, 32767\right)$$

This maximizes the 16-bit dynamic range without integer clipping during normal acoustic excitation.

---

## 6. FreeRTOS Multi-Core Concurrency Model

```
               CORE 0 (System Core)              CORE 1 (Application Core)
          +-----------------------------+     +-----------------------------+
          |  WiFi Driver & TCP/IP Stack |     |  audioTask (Priority 5)     |
          |  PlatformIO Serial Dispatch |     |   - i2s_read() [Blocking]   |
          |  FreeRTOS Idle / Tick Tasks |     |   - Decimate by 4           |
          |                             |     |   - Scale & Saturate        |
          |                             |     |   - Serial.println()        |
          +-----------------------------+     +-----------------------------+
                                                             |
                                                             | vTaskDelay(1000)
                                                             v
                                              +-----------------------------+
                                              |  Arduino loop() (Priority 1)|
                                              |   - Housekeeping & Stats    |
                                              +-----------------------------+
```

1. **Deterministic DMA Reads**: `audioTask` is pinned exclusively to **Core 1** at priority 5.
2. **Zero Block on Core 0**: Core 0 is reserved for ESP32 background duties (WiFi, RF calibration, Bluetooth).
3. **Task Yielding**: Calls to `taskYIELD()` ensure equal-priority application threads on Core 1 receive fair time-slicing.
