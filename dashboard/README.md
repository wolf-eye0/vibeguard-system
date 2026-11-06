# VibeGuard Real-Time Condition Monitoring Dashboard & Telemetry Bridge

## Overview
Presentation-ready local condition-monitoring dashboard and telemetry bridge for the VibeGuard edge monitoring system (ESP32 + ADXL345 motor rig). 

It features an animated **Digital Twin Isometric Visualizer**, real-time **Chart.js Multi-Timeframe Vibration Graphs**, tri-axial directional monitors (**X, Y, Z**), ISO 10816-3 threshold indicators, and one-click **Session CSV Export**.

---

## Quick Start (Node.js Server & Native Bridge)

### 1. Install Dependencies
```bash
npm install
```

### 2. Launch with Connected Hardware (Live Mode)
```bash
SERIAL_PORT=/dev/ttyUSB0 npm start
```
* Or for auto port detection:
```bash
AUTO_SERIAL=true npm start
```

### 3. Open in Browser
* **Live Mode:** `http://127.0.0.1:8765/vibeguard-dashboard.html?source=live`
* **Offline Preview Mode:** `http://127.0.0.1:8765/vibeguard-dashboard.html?source=preview`

---

## Alternative: Python WebSocket Bridge (`bridge.py`)
If you prefer running a lightweight Python bridge:
```bash
pip install -r requirements.txt
python3 bridge.py --port /dev/ttyUSB0
```
This serves real-time WebSocket telemetry at `ws://127.0.0.1:8765`.

---

## Key Features & Keyboard Shortcuts
* **`P` Key:** Toggle **Presentation Mode** (clean full-screen display for project reviews and viva).
* **Pause Button:** Freezes the live display for closer inspection while background data acquisition continues.
* **Theme Toggle:** Switch between Dark and Light mode.
* **Export Session CSV:** One-click download of all collected time-series telemetry samples.
* **Interactive Digital Twin:** SVG rig visualization animates and colors in real-time according to measured vibration severity.
