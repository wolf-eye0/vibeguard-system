# VibeGuard: Autonomous Rotordynamic Vibration Monitoring & Fault Diagnostic System

[![Platform](https://img.shields.io/badge/Platform-ESP32--WROOM--32E-blue.svg)](https://www.espressif.com/)
[![Sensor](https://img.shields.io/badge/Sensor-ADXL345%20(SPI%20Mode%202)-brightgreen.svg)](https://www.analog.com/)
[![Sampling](https://img.shields.io/badge/ODR-800%20Hz-orange.svg)](#dsp-pipeline)
[![Simulation](https://img.shields.io/badge/Wokwi%20Tests-64%2F64%20Passed-success.svg)](./simulation/wokwi/)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

**VibeGuard** is an edge-computing cyber-physical condition monitoring system designed for rotating machinery. It pairs an **ESP32 microcontroller** with an **ADXL345 triaxial accelerometer** over a dedicated 4-wire SPI bus running at **800 Hz Output Data Rate (ODR)** to perform real-time vibration analytics, background noise tare subtraction, condition classification (Green / Yellow / Red), and live telemetry streaming to an interactive web dashboard.

---

## System Overview

```
 [ Rotating Asset ] ── (Mechanical Coupling) ──► [ ADXL345 Accelerometer ]
                                                         │ (4-Wire SPI Mode 2 @ 800 Hz)
                                                         ▼
                                                [ ESP32 Edge Core ]
                                                 ├─ Welford O(1) Running Variance
                                                 ├─ Quadrature Tare Floor Removal
                                                 ├─ Vector RMS Triaxial Fusion
                                                 ├─ Hysteresis Condition Classifier
                                                 ├─ NVS Flash Parameter Storage
                                                 └─ Optical LED Triage (RGB State)
                                                         │ (USB Serial @ 115200 baud)
                                                         ▼
                                            [ Node.js Telemetry Bridge ]
                                                         │ (WebSocket)
                                                         ▼
                                            [ Real-Time Web Dashboard ]
                                            (12-Col HUD & 3D Digital Twin)
```

### Key Technical Highlights
- **High-Speed Sensor Interface:** Native 4-Wire SPI Mode 2 (`CPOL=1, CPHA=1`) at 800 Hz eliminates $I^2C$ bus bottleneck and FIFO under-runs.
- **Quadrature Tare Floor Removal:** Removes the physical rig's static noise floor using energy-domain subtraction:
  $$V_{\text{net}} = \sqrt{\max\left(0, V_{\text{raw}}^2 - V_{\text{floor}}^2\right)}$$
- **Streaming Field Auto-Calibration:** 15-second Welford algorithm running accumulation with zero array allocation ($O(1)$ memory).
- **Non-Volatile Thresholds:** Persists baseline calibration and trip levels directly into ESP32 NVS flash across power cycles.
- **Zero-Dependency 3D Digital Twin:** Standalone WebGL Three.js twin for interactive simulation and physics-based orbital visualization.

---

## Hardware Specifications & Wiring

### ADXL345 to ESP32 Pin Mapping

| ADXL345 Pin | ESP32 GPIO | Description | Wire Color |
|---|---|---|---|
| **VCC** | `3V3` | Regulated 3.3V Power | Red |
| **GND** | `GND` | Ground Reference | Black |
| **CS** | `GPIO 5` | Hardware Chip Select (Active Low) | White |
| **SDO / MISO** | `GPIO 19` | Master-In-Slave-Out | Blue |
| **SDA / MOSI** | `GPIO 23` | Master-Out-Slave-In | Yellow |
| **SCL / SCK** | `GPIO 18` | SPI Serial Clock | Green |

### Diagnostic Outputs
- **Optical Triage LED:** `GPIO 2` (Onboard Blue/Purple for Status & Calibration Pulsing) / External RGB.
- **Relay / Trip Trigger:** `GPIO 4` (Configurable for motor power cutoff).

---

## Quick Start Guide

### 1. Flash the ESP32 Firmware

Using **Arduino IDE** or **Arduino CLI**:

1. Open [`firmware/VibeGuard_Autotune_Synchronized/VibeGuard_Autotune_Synchronized.ino`](./firmware/VibeGuard_Autotune_Synchronized/VibeGuard_Autotune_Synchronized.ino).
2. Select Board: **ESP32 Dev Module** (Partition Scheme: Default 4MB with SPIFFS/NVS).
3. Connect your ESP32 via USB and select the COM port (e.g., `/dev/ttyUSB0` on Linux or `COM3` on Windows).
4. Click **Upload**.

Or using CLI:
```bash
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/VibeGuard_Autotune_Synchronized/
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 firmware/VibeGuard_Autotune_Synchronized/
```

---

### 2. Launch the Real-Time Web Dashboard

The web dashboard bridges the ESP32 USB serial stream over WebSockets to a modern browser UI.

```bash
cd dashboard
npm install
npm start
```
Open **`http://localhost:3000`** in your browser. The bridge will automatically detect `/dev/ttyUSB0` and begin displaying live triaxial waveforms, spectrum bars, and machine condition.

---

### 3. Open the 3D WebGL Digital Twin

No server required! Simply double-click and open:
[`simulation/VibeGuard_3D_Digital_Twin.html`](./simulation/VibeGuard_3D_Digital_Twin.html)

This interactive 3D twin allows you to inspect:
- 3D CAD hierarchy (N20 motor, rotor arm, D-shaft, sensor bracket).
- Dynamic orbital precession, Lissajous orbits, and synthetic fault injection (imbalance, misalignment, bearing looseness).

---

### 4. Run the Wokwi Simulation Tests

We maintain a full test suite of **64 automated tests** (covering hardware register interfaces, boundary conditions, and end-to-end telemetry):

```bash
cd simulation/wokwi/tests
node run_all_tests.js
```
Expected output: **64/64 PASSED (100.0%)**.

---

## Repository Structure

```
vibeguard-system/
├── firmware/
│   ├── VibeGuard_Autotune_Synchronized/ # Main ESP32 production sketch & DSP engine
│   └── VibeGuard_Benchmark_Logger/      # Micro-benchmarks for latency and throughput
├── dashboard/
│   ├── server.js                        # Serial-to-WebSocket Node.js bridge
│   └── public/                          # 12-column real-time telemetry HUD & charts
├── simulation/
│   ├── VibeGuard_3D_Digital_Twin.html   # Standalone WebGL 3D interactive twin
│   └── wokwi/                           # Wokwi simulation models and 64 automated tests
├── hardware/
│   ├── cad_models/                      # 3D printable STL files (mounts, shims, rotor arms)
│   ├── schematics/                      # Master circuit diagrams, breadboard wiring (PNG/SVG)
│   └── templates_and_measurements/      # 1:1 drill sticker template & vernier caliper guides
└── docs/
    ├── VIBEGUARD_ASD_STE100_TECHNICAL_SPECIFICATION_AND_DEFENSE_GUIDE.pdf
    ├── VIBEGUARD_EXPO_CHART_INFOGRAPHIC_SOURCE.md
    └── VibeGuard_Assembly_Tuning_and_Dashboard_Handoff_Guide.pdf
```

---

## Mechanical Fabrication & 3D Printing

All 3D models in [`hardware/cad_models/`](./hardware/cad_models/) are certified for direct FDM printing (PLA/PETG, 0.20 mm layer height, 100% infill for sensor rigidity):
- `VibeGuard_ADXL345_Rigid_Mount.stl` — High-stiffness bracket for accelerometer mounting.
- `VibeGuard_Rotor_Arm_D_Shaft_3p40mm.stl` — Precision motor shaft hub for eccentric weights.
- `VibeGuard_N20_Motor_Shim_Pad_1mm.stl` & `1p2mm.stl` — Height calibration shims for motor alignment.

Drill template for the acrylic baseplate is available in [`hardware/templates_and_measurements/VibeGuard_Baseplate_1to1_Drill_Sticker_Template.pdf`](./hardware/templates_and_measurements/VibeGuard_Baseplate_1to1_Drill_Sticker_Template.pdf).

---

## Project Context & Academic Credits

Developed as part of the **B.Tech Computer Science & Engineering Capstone Project**:
- **Institution:** Jyothi Engineering College (Autonomous), Cheruthuruthy, Thrissur, Kerala.
- **Affiliation:** APJ Abdul Kalam Technological University (KTU).
- **Faculty Guide:** Ms. Sagna L T (Assistant Professor, Dept. of CSE).
- **Development Team:**
  - Sreehari S
  - Archa Anil
  - Sreeprada V
  - ParadoxPete (Nihad P C)

---

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
