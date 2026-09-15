# Enterprise Scaling & Deployment Guide

This guide details the architectural pathways for scaling the **ESP32 + INMP441** audio acquisition engine from a single-device telemetry prototype into a distributed, multi-node edge ML acoustic network.

---

## 1. Multi-Microphone Arrays & Beamforming

### 1.1 Stereo Dual-Microphone Expansion (Single ESP32)
The INMP441 is designed for time-multiplexed stereo operation on a single shared I2S bus:

```
                      +-------------------+
                      |  ESP32-WROOM-32D  |
                      +--+------+------+--+
                         |      |      |
             +-----------+      |      +-----------+
      SCK    |           WS     |           SD     |
  (GPIO 14)  |       (GPIO 15)  |       (GPIO 32)  |
             |                  |                  |
    +--------+--------+        |         +--------+--------+
    | INMP441 #1      |        |         | INMP441 #2      |
    | (Mic Left)      |        |         | (Mic Right)     |
    |                 |        |         |                 |
    | SCK <-----------+        +-------> | SCK             |
    | WS  <--------------------+-------> | WS              |
    | SD  ---------------------+-------> | SD              |
    |                 |                  |                 |
    | L/R -> GND      |                  | L/R -> 3V3      |
    +-----------------+                  +-----------------+
```

#### Shared Bus Mechanics
- **Common Clocks**: Both microphones share the same `SCK` (BCLK) and `WS` (LRCLK) signals.
- **Shared Data Line**: Because `L/R` on Mic #1 is tied to **GND**, it drives the `SD` line during `WS = Low` and enters high-impedance (`Hi-Z`) during `WS = High`. Mic #2 (`L/R` tied to **3V3**) drives `SD` during `WS = High` and enters `Hi-Z` during `WS = Low`.
- **Zero Additional GPIOs**: A complete 2-channel stereo acoustic array requires zero extra pins on the ESP32.

#### Acoustic Applications
1. **Time Difference of Arrival (TDOA)**:
   Given distance $d$ between microphone ports and speed of sound $c \approx 343\text{ m/s}$:
   $$\Delta t = \frac{d \cdot \cos(\theta)}{c}$$
   Enables 2D sound source angle estimation $\theta$ ($0^\circ \dots 180^\circ$).
2. **Differential Noise Cancellation**:
   Subtracting ambient background noise captured by a rear-facing microphone from the voice-facing microphone.

---

## 2. Edge Impulse ML Model Deployment

Once you have gathered your dataset in Edge Impulse Studio, export the trained classifier directly into this firmware.

### 2.1 Deployment Pipeline
```
[Edge Impulse Studio] ===> [C++ Library Export] ===> [PlatformIO lib/] ===> [Zero-Copy Inference]
```

1. In Edge Impulse Studio, navigate to **Deployment** $\rightarrow$ Select **C++ Library**.
2. Download the generated `.zip` archive.
3. Extract the contents into the `lib/` directory:
   ```
   esp32-inmp441-mic/
   └── lib/
       └── edge-impulse-sdk/
   ```

### 2.2 Double-Buffer Streaming Inference Architecture
To ensure continuous audio ingestion while running inference on the CPU, employ a ping-pong double-buffer:

```cpp
// Ping-Pong Double Buffer
static int16_t inference_buffer_A[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
static int16_t inference_buffer_B[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
static volatile int active_buffer = 0;

void audioCaptureTask(void* pvParameters) {
    while (true) {
        int16_t* target_buf = (active_buffer == 0) ? inference_buffer_A : inference_buffer_B;
        
        // Fill active buffer from I2S DMA...
        audioInput.read(temp_buf, CHUNK_SIZE);
        
        // Swap buffers and signal inference task
        active_buffer = 1 - active_buffer;
        xSemaphoreGive(inferenceTriggerSemaphore);
    }
}

void inferenceTask(void* pvParameters) {
    while (true) {
        xSemaphoreTake(inferenceTriggerSemaphore, portMAX_DELAY);
        int16_t* ready_buf = (active_buffer == 0) ? inference_buffer_B : inference_buffer_A;
        
        // Run Edge Impulse DSP + Neural Network
        signal_t signal;
        numpy::signal_from_buffer(ready_buf, EI_CLASSIFIER_RAW_SAMPLE_COUNT, &signal);
        
        ei_impulse_result_t result = { 0 };
        run_classifier(&signal, &result, false);
        
        // Output classification results (e.g. glass_break: 0.94)
    }
}
```

---

## 3. Flash, Memory & Performance Optimization

### 3.1 Partition Table Configuration
Standard ESP32 factory partitions provide only ~1.25 MB of program space. Large Edge Impulse convolutional models (CNN) or spectrogram buffers can exceed this limit.

Create a custom `partitions.csv` in your project root:
```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1E0000,
app1,     app,  ota_1,   0x1F0000,0x1E0000,
spiffs,   data, spiffs,  0x3D0000,0x30000,
```
Add to `platformio.ini`:
```ini
board_build.partitions = partitions.csv
```
This increases available application flash space to **1.9 MB per OTA slot**.

### 3.2 Accelerating DSP Math with ESP-DSP
Espressif provides the hardware-accelerated `esp-dsp` library optimized with Xtensa SIMD assembly instructions:
```ini
; platformio.ini
lib_deps =
    espressif/esp-dsp @ ^1.4.0
```
- **Vector Multiplication**: $4\times$ speedup over standard GCC software floating-point loops.
- **Radix-2 / Radix-4 Real FFT**: Executes 1024-point FFT in **under 0.8 ms** at 240 MHz.

### 3.3 Power Management & Sleep Optimization
For battery-operated remote IoT sensor nodes:
1. **Light Sleep with DMA Auto-Wake**:
   Configure automatic light sleep when FreeRTOS tasks are idle:
   ```cpp
   esp_pm_config_esp32_t pm_config = {
       .max_freq_mhz = 240,
       .min_freq_mhz = 80,
       .light_sleep_enable = true
   };
   esp_pm_configure(&pm_config);
   ```
2. **Threshold Wake-Up (Voice Activity Detection)**:
   Keep the CPU in light sleep; evaluate incoming audio RMS power in a low-frequency FreeRTOS tick. If amplitude exceeds a calibrated noise threshold (e.g., $>-35\text{ dBFS}$), ramp CPU to 240 MHz and trigger Edge Impulse classification.

