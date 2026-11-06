# VibeGuard Interactive Benchmark Logger & Telemetry Firmware

## Overview
This firmware runs on the **7Semi ESP32-DEVKIT-E** and interfaces with the **Analog Devices ADXL345 3-Axis Digital Accelerometer** over high-speed hardware SPI. It performs real-time edge vibration monitoring, dynamic gravity DC offset removal, vector RMS acceleration calculation, and ISO 10816-3 machinery vibration classification.

---

## Hardware Pinout
The hardware SPI bus is configured in **SPI Mode 2** (CPOL=1, CPHA=0) certified for Silicon ID `0xE5`:

| Signal Name | ADXL345 Pin | ESP32 GPIO | Description |
| :--- | :--- | :--- | :--- |
| **CS** | CS | **GPIO 21** | Chip Select (Active LOW) |
| **SCK** | SCL | **GPIO 18** | Hardware SPI Clock (1 MHz) |
| **MISO** | SDO | **GPIO 19** | Master In Slave Out |
| **MOSI** | SDA | **GPIO 23** | Master Out Slave In |
| **LED Green** | Green Anode | **GPIO 25** | Normal Operation (Zone A / B) |
| **LED Blue** | Blue Anode | **GPIO 26** | Booting / Baseline Calibration |
| **LED Red** | Red Anode | **GPIO 27** | Unbalance Critical Alarm (Zone D) |

---

## Vibration Thresholds (ISO 10816-3)
* **Sampling Rate:** 800 Hz ODR (128 samples per window = 160 ms window)
* **Normal (Green):** $\text{VRMS} < 0.35\text{ g}$
* **Warning (Yellow):** $0.35\text{ g} \le \text{VRMS} < 0.70\text{ g}$
* **Critical Alarm (Red):** $\text{VRMS} \ge 0.70\text{ g}$ (Debounce filter: $\ge 2$ consecutive windows)

---

## Interactive Serial Commands
Open Serial Monitor at **115200 baud** and send:
* `0` or `b`: Record **Stage 0: Baseline Run** (Bare Arm, 0 Nuts)
* `1`: Record **Stage 1: Mild Unbalance** (M3 Bolt alone)
* `2`: Record **Stage 2: Moderate Unbalance** (Bolt + 1 Nut)
* `3`: Record **Stage 3: Severe Unbalance** (Bolt + 2 Nuts)
* `s` or `t`: Print **Experimental Benchmark Summary Table** (ready for project reports)
* `c`: Clear benchmark records
* `?` or `h`: Print interactive help menu

---

## Flashing Instructions
To compile and flash from terminal:
```bash
./auto_flash_esp32.sh
```
Or open `VibeGuard_Benchmark_Logger.ino` directly in Arduino IDE 2.x and click **Upload**.
