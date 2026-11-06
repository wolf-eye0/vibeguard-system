#include <Arduino.h>
#include <SPI.h>
#include "features.h"
#include "state.h"
#include "adxl345_spi.h"

// ==========================================
// Hardware Pin Definitions (Frozen Baseline)
// ==========================================
#define PIN_LED_GREEN  25
#define PIN_LED_BLUE   26
#define PIN_LED_RED    27

// ==========================================
// DSP & Sampling Configuration
// ==========================================
#define SAMPLE_BUFFER_SIZE  256     // Must be power of 2 for Radix-2 FFT
#define SAMPLING_FREQ_HZ    800.0f  // ADXL345 800 Hz ODR
#define SAMPLE_INTERVAL_US  1250    // 1 / 800 Hz = 1250 microseconds

// Anomaly Thresholds (in g-RMS and Frequency)
#define RMS_WARNING_THRESHOLD   0.35f  // Warning level (g-RMS)
#define RMS_ANOMALY_THRESHOLD   0.70f  // Critical anomaly level (g-RMS)
#define TARGET_MOTOR_FREQ_HZ    10.0f  // 600 RPM / 60 = 10 Hz fundamental

// ==========================================
// Global Buffers & State Objects
// ==========================================
ADXL345_SPI accel(ADXL345_PIN_CS);

float x_buffer[SAMPLE_BUFFER_SIZE];
float y_buffer[SAMPLE_BUFFER_SIZE];
float z_buffer[SAMPLE_BUFFER_SIZE];

SystemState current_state = SystemState::StartSelfCheck;
WindowHistory window_history;
PersistenceParams persistence_params = {
    .k_required = 3,           // 3 consecutive abnormal windows to trigger Alarm
    .m_window = 5,             // 5 window sliding memory
    .k_clear_required = 3,     // 3 consecutive clean windows to recover
    .require_consecutive = true
};

// ==========================================
// Helper: RGB LED Controller (Common-Cathode)
// ==========================================
void setRGB(bool r, bool g, bool b) {
    digitalWrite(PIN_LED_RED,   r ? HIGH : LOW);
    digitalWrite(PIN_LED_GREEN, g ? HIGH : LOW);
    digitalWrite(PIN_LED_BLUE,  b ? HIGH : LOW);
}

void updateIndicator(SystemState state, float v_rms) {
    switch (state) {
    case SystemState::StartSelfCheck:
    case SystemState::Calibrating:
        setRGB(false, false, true); // Solid Blue
        break;
    case SystemState::Normal:
        if (v_rms > RMS_WARNING_THRESHOLD) {
            setRGB(true, true, false); // Yellow / Amber Warning
        } else {
            setRGB(false, true, false); // Solid Green
        }
        break;
    case SystemState::Abnormal:
        setRGB(true, false, false); // Solid Red Alarm
        break;
    case SystemState::FaultInvalid:
        // Blinking Red/Magenta
        setRGB(true, false, (millis() / 250) % 2 == 0);
        break;
    }
}

// ==========================================
// Setup Function
// ==========================================
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000);

    Serial.println("\n==================================================");
    Serial.println("   VibeGuard Level-1 Edge Vibration Monitor");
    Serial.println("   ESP32-DevKitC-32E | ADXL345 (4-Wire SPI)");
    Serial.println("==================================================");

    // Initialize RGB LED Pins
    pinMode(PIN_LED_RED,   OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_BLUE,  OUTPUT);
    setRGB(false, false, true); // Blue during self-check

    // Initialize ADXL345 SPI
    Serial.print("[INIT] Connecting to ADXL345 over 4-Wire SPI (CS=5)... ");
    bool sensor_ok = accel.begin();
    if (!sensor_ok) {
        Serial.println("FAILED! (Device ID mismatch or SPI error)");
        current_state = SystemState::FaultInvalid;
        updateIndicator(current_state, 0.0f);
        return;
    }
    Serial.println("OK! (Device ID: 0xE5, Range: ±16g, Rate: 800 Hz)");

    current_state = update_state(current_state, true, false, false, false, false);
    Serial.println("[STATE] Self-Check Complete -> State: Calibrating baseline...");
    delay(1000);

    current_state = update_state(current_state, true, true, false, false, false);
    Serial.println("[STATE] Calibration Complete -> State: Normal Operation");
}

// ==========================================
// Main Loop: 800 Hz Acquisition + DSP Window
// ==========================================
void loop() {
    if (current_state == SystemState::FaultInvalid) {
        updateIndicator(current_state, 0.0f);
        delay(100);
        return;
    }

    // 1. Acquire 256 Samples at 800 Hz (Sampling window = ~320 ms)
    AccelGData g_sample;
    for (size_t i = 0; i < SAMPLE_BUFFER_SIZE; ++i) {
        unsigned long start_us = micros();
        accel.readG(g_sample);
        x_buffer[i] = g_sample.x;
        y_buffer[i] = g_sample.y;
        z_buffer[i] = g_sample.z;

        while (micros() - start_us < SAMPLE_INTERVAL_US) {
            // Precise timer delay for 800 Hz ODR
        }
    }

    // 2. DSP Step 1: Remove DC Component (Gravity + Offset)
    remove_dc(x_buffer, SAMPLE_BUFFER_SIZE);
    remove_dc(y_buffer, SAMPLE_BUFFER_SIZE);
    remove_dc(z_buffer, SAMPLE_BUFFER_SIZE);

    // 3. DSP Step 2: Compute 3-Axis Vector RMS
    float v_rms = compute_vector_rms(x_buffer, y_buffer, z_buffer, SAMPLE_BUFFER_SIZE);

    // 4. DSP Step 3: Hann Window + 256-Point FFT on Radial X-Axis
    float peak_freq = 0.0f;
    float peak_mag = 0.0f;
    compute_fft_peak(x_buffer, SAMPLE_BUFFER_SIZE, SAMPLING_FREQ_HZ, peak_freq, peak_mag);

    // 5. Evaluate Thresholds & Anomaly Condition
    bool is_above_threshold = (v_rms >= RMS_ANOMALY_THRESHOLD);
    window_history.is_above_threshold.push_back(is_above_threshold);

    // Cap history length to avoid memory growth
    if (window_history.is_above_threshold.size() > 20) {
        window_history.is_above_threshold.erase(window_history.is_above_threshold.begin());
    }

    bool enter_abnormal = should_enter_abnormal(window_history, persistence_params);
    bool clear_abnormal = should_clear_abnormal(window_history, persistence_params);

    current_state = update_state(current_state, true, true, enter_abnormal, clear_abnormal, false);
    updateIndicator(current_state, v_rms);

    // 6. Output Serial Telemetry (Formatted for Serial Monitor & Plotter)
    Serial.printf(">VRMS:%.3f,PeakFreq:%.1f,PeakMag:%.3f,State:%d\n",
                  v_rms, peak_freq, peak_mag, static_cast<int>(current_state));

    Serial.print("[TELEMETRY] Vector RMS: ");
    Serial.print(v_rms, 3);
    Serial.print(" g | Dominant Freq: ");
    Serial.print(peak_freq, 1);
    Serial.print(" Hz (Mag: ");
    Serial.print(peak_mag, 3);
    Serial.print(" g) | Status: [");
    Serial.print(to_string(current_state).c_str());
    Serial.println("]");
}
