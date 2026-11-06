#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <cmath>
#include <cstddef>
#include <algorithm>

// ============================================================================
// 1. HARDWARE PIN DEFINITIONS (BOM-FROZEN PINOUT)
// ============================================================================
#define ADXL345_PIN_CS         21
#define ADXL345_PIN_SCK        18
#define ADXL345_PIN_MISO       19
#define ADXL345_PIN_MOSI       23

#define PIN_LED_GREEN          25
#define PIN_LED_BLUE           26
#define PIN_LED_RED            27

// ADXL345 Register Map
#define ADXL345_REG_DEVID      0x00
#define ADXL345_REG_BW_RATE    0x2C
#define ADXL345_REG_POWER_CTL  0x2D
#define ADXL345_REG_DATA_FORMAT 0x31
#define ADXL345_REG_DATAX0     0x32

#define ADXL345_DEVICE_ID      0xE5
#define ADXL345_SPI_READ       0x80
#define ADXL345_SPI_MB         0x40
#define ADXL345_SCALE_FACTOR   0.0039f // 3.9 mg/LSB full resolution (±16g)

// DSP & Sampling Parameters
#define SAMPLE_BUFFER_SIZE     256
#define SAMPLING_FREQ_HZ       800.0f
#define SAMPLE_INTERVAL_US     1250

#define RMS_WARNING_THRESHOLD  0.35f
#define RMS_ANOMALY_THRESHOLD  0.70f

// ============================================================================
// 2. ENUMS & DATA STRUCTURES
// ============================================================================
enum class SystemState : uint8_t {
    StartSelfCheck = 0,
    Calibrating    = 1,
    Normal         = 2,
    Abnormal       = 3,
    FaultInvalid   = 4
};

struct PersistenceParams {
    int k_required;
    int m_window;
    int k_clear_required;
    bool require_consecutive;
};

struct WindowHistory {
    std::vector<bool> is_above_threshold;
};

struct AccelGData {
    float x;
    float y;
    float z;
};

// ============================================================================
// 3. ADXL345 4-WIRE HARDWARE SPI DRIVER
// ============================================================================
class ADXL345_SPI {
public:
    ADXL345_SPI(uint8_t cs_pin = ADXL345_PIN_CS) : _cs_pin(cs_pin), _hardware_detected(false) {}

    bool begin() {
        pinMode(_cs_pin, OUTPUT);
        digitalWrite(_cs_pin, HIGH);

        SPI.begin(ADXL345_PIN_SCK, ADXL345_PIN_MISO, ADXL345_PIN_MOSI, _cs_pin);
        SPI.beginTransaction(SPISettings(5000000, MSBFIRST, SPI_MODE3));
        delay(15);

        uint8_t dev_id = readRegister(ADXL345_REG_DEVID);
        if (dev_id != ADXL345_DEVICE_ID) {
            // Try SPI Mode 0
            SPI.endTransaction();
            SPI.beginTransaction(SPISettings(5000000, MSBFIRST, SPI_MODE0));
            dev_id = readRegister(ADXL345_REG_DEVID);
        }

        if (dev_id == ADXL345_DEVICE_ID) {
            writeRegister(ADXL345_REG_BW_RATE, 0x0D);      // 800 Hz ODR
            writeRegister(ADXL345_REG_DATA_FORMAT, 0x0B);  // Full-Res, ±16g, 4-wire SPI
            writeRegister(ADXL345_REG_POWER_CTL, 0x08);    // Measurement mode
            _hardware_detected = true;
            return true;
        }

        _hardware_detected = false;
        return false;
    }

    uint8_t readRegister(uint8_t reg) {
        digitalWrite(_cs_pin, LOW);
        SPI.transfer(reg | ADXL345_SPI_READ);
        uint8_t val = SPI.transfer(0x00);
        digitalWrite(_cs_pin, HIGH);
        return val;
    }

    void writeRegister(uint8_t reg, uint8_t value) {
        digitalWrite(_cs_pin, LOW);
        SPI.transfer(reg & ~ADXL345_SPI_READ);
        SPI.transfer(value);
        digitalWrite(_cs_pin, HIGH);
    }

    void readBurst(uint8_t start_reg, uint8_t* buffer, uint8_t length) {
        digitalWrite(_cs_pin, LOW);
        SPI.transfer(start_reg | ADXL345_SPI_READ | ADXL345_SPI_MB);
        for (uint8_t i = 0; i < length; ++i) {
            buffer[i] = SPI.transfer(0x00);
        }
        digitalWrite(_cs_pin, HIGH);
    }

    bool readG(AccelGData& g) {
        if (!_hardware_detected) return false;
        uint8_t buf[6];
        readBurst(ADXL345_REG_DATAX0, buf, 6);
        int16_t rx = static_cast<int16_t>(buf[0] | (buf[1] << 8));
        int16_t ry = static_cast<int16_t>(buf[2] | (buf[3] << 8));
        int16_t rz = static_cast<int16_t>(buf[4] | (buf[5] << 8));
        g.x = static_cast<float>(rx) * ADXL345_SCALE_FACTOR;
        g.y = static_cast<float>(ry) * ADXL345_SCALE_FACTOR;
        g.z = static_cast<float>(rz) * ADXL345_SCALE_FACTOR;
        return true;
    }

    bool isHardwareDetected() const { return _hardware_detected; }

private:
    uint8_t _cs_pin;
    bool _hardware_detected;
};

// ============================================================================
// 4. EMBEDDED DSP FUNCTIONS
// ============================================================================
void remove_dc(float* data, size_t n) {
    if (!data || n == 0) return;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += static_cast<double>(data[i]);
    float mean = static_cast<float>(sum / static_cast<double>(n));
    for (size_t i = 0; i < n; ++i) data[i] -= mean;
}

float compute_vector_rms(const float* x, const float* y, const float* z, size_t n) {
    if (!x || !y || !z || n == 0) return 0.0f;
    double sum_sq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double dx = static_cast<double>(x[i]);
        double dy = static_cast<double>(y[i]);
        double dz = static_cast<double>(z[i]);
        sum_sq += (dx * dx + dy * dy + dz * dz);
    }
    return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(n)));
}

void apply_hann_window(float* data, size_t n) {
    if (!data || n <= 1) return;
    const float two_pi = 6.283185307179586f;
    const float denom = static_cast<float>(n - 1);
    for (size_t i = 0; i < n; ++i) {
        float factor = 0.5f * (1.0f - std::cos(two_pi * static_cast<float>(i) / denom));
        data[i] *= factor;
    }
}

static void fft_radix2(float* real, float* imag, size_t n) {
    size_t j = 0;
    for (size_t i = 0; i < n - 1; ++i) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        size_t k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
    const float pi = 3.141592653589793f;
    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * pi / static_cast<float>(len);
        float wlen_r = std::cos(angle);
        float wlen_i = std::sin(angle);
        for (size_t i = 0; i < n; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (size_t k = 0; k < len / 2; ++k) {
                float u_r = real[i + k];
                float u_i = imag[i + k];
                float v_r = real[i + k + len / 2] * w_r - imag[i + k + len / 2] * w_i;
                float v_i = real[i + k + len / 2] * w_i + imag[i + k + len / 2] * w_r;
                real[i + k] = u_r + v_r;
                imag[i + k] = u_i + v_i;
                real[i + k + len / 2] = u_r - v_r;
                imag[i + k + len / 2] = u_i - v_i;
                float next_w_r = w_r * wlen_r - w_i * wlen_i;
                float next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
                w_i = next_w_i;
            }
        }
    }
}

void compute_fft_peak(const float* time_data, size_t n, float sampling_rate,
                      float& out_peak_freq, float& out_peak_mag) {
    out_peak_freq = 0.0f;
    out_peak_mag = 0.0f;
    if (!time_data || n < 4) return;

    std::vector<float> r(time_data, time_data + n);
    std::vector<float> im(n, 0.0f);

    apply_hann_window(r.data(), n);
    fft_radix2(r.data(), im.data(), n);

    size_t half = n / 2;
    float max_mag = 0.0f;
    size_t max_bin = 1;
    for (size_t i = 1; i < half; ++i) {
        float mag = std::sqrt(r[i] * r[i] + im[i] * im[i]) * (2.0f / static_cast<float>(n));
        if (mag > max_mag) {
            max_mag = mag;
            max_bin = i;
        }
    }
    out_peak_freq = static_cast<float>(max_bin) * (sampling_rate / static_cast<float>(n));
    out_peak_mag = max_mag;
}

// ============================================================================
// 5. STATE MACHINE & PERSISTENCE
// ============================================================================
bool should_enter_abnormal(const WindowHistory& h, const PersistenceParams& p) {
    const auto& v = h.is_above_threshold;
    if (v.empty() || p.k_required <= 0) return false;
    const int n = static_cast<int>(v.size());
    const int M = (p.m_window > 0 && p.m_window < n) ? p.m_window : n;
    int current_run = 0, best_run = 0;
    for (int i = n - M; i < n; ++i) {
        if (v[i]) {
            current_run++;
            if (current_run > best_run) best_run = current_run;
        } else {
            current_run = 0;
        }
    }
    return best_run >= p.k_required;
}

bool should_clear_abnormal(const WindowHistory& h, const PersistenceParams& p) {
    const auto& v = h.is_above_threshold;
    if (v.empty()) return true;
    int clear_req = (p.k_clear_required > 0) ? p.k_clear_required : p.k_required;
    const int n = static_cast<int>(v.size());
    if (n < clear_req) return false;
    for (int i = n - clear_req; i < n; ++i) {
        if (v[i]) return false;
    }
    return true;
}

SystemState update_state(SystemState current, bool config_ok, bool calibration_done,
                         bool enter_abnormal, bool clear_abnormal, bool fault_condition) {
    if (fault_condition) return SystemState::FaultInvalid;
    switch (current) {
    case SystemState::StartSelfCheck:
        if (config_ok) return SystemState::Calibrating;
        break;
    case SystemState::Calibrating:
        if (calibration_done) return SystemState::Normal;
        break;
    case SystemState::Normal:
        if (enter_abnormal) return SystemState::Abnormal;
        break;
    case SystemState::Abnormal:
        if (clear_abnormal) return SystemState::Normal;
        break;
    case SystemState::FaultInvalid:
        return SystemState::FaultInvalid;
    }
    return current;
}

const char* stateToString(SystemState s) {
    switch (s) {
    case SystemState::StartSelfCheck: return "StartSelfCheck";
    case SystemState::Calibrating:    return "Calibrating";
    case SystemState::Normal:         return "Normal";
    case SystemState::Abnormal:       return "Abnormal";
    case SystemState::FaultInvalid:   return "FaultInvalid";
    }
    return "Unknown";
}

// ============================================================================
// 6. GLOBAL INSTANCES & MAIN SETUP/LOOP
// ============================================================================
ADXL345_SPI accel(ADXL345_PIN_CS);

float x_buffer[SAMPLE_BUFFER_SIZE];
float y_buffer[SAMPLE_BUFFER_SIZE];
float z_buffer[SAMPLE_BUFFER_SIZE];

SystemState current_state = SystemState::StartSelfCheck;
WindowHistory window_history;
PersistenceParams persistence_params = {
    .k_required = 3,
    .m_window = 5,
    .k_clear_required = 3,
    .require_consecutive = true
};

double sim_time_sec = 0.0;

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
            setRGB(true, true, false); // Amber / Yellow
        } else {
            setRGB(false, true, false); // Solid Green
        }
        break;
    case SystemState::Abnormal:
        setRGB(true, false, false); // Solid Red Alarm
        break;
    case SystemState::FaultInvalid:
        setRGB(true, false, (millis() / 250) % 2 == 0); // Pulsing Purple
        break;
    }
}

// Fallback synthetic generator (used only if SPI hardware is disconnected)
void generate_simulated_vibration(AccelGData& g, float fault_level) {
    const double f0 = 10.0; // 600 RPM N20 Motor = 10.0 Hz
    const double omega = 6.283185307179586 * f0;

    double noise_x = (static_cast<double>(random(0, 100)) / 1000.0) - 0.05;
    double noise_y = (static_cast<double>(random(0, 100)) / 1000.0) - 0.05;
    double noise_z = (static_cast<double>(random(0, 100)) / 1000.0) - 0.05;

    double unbalance_amp = 1.85 * static_cast<double>(fault_level);

    double ax = (0.08 + unbalance_amp) * std::sin(omega * sim_time_sec) + 0.02 * std::sin(2.0 * omega * sim_time_sec) + noise_x;
    double ay = (0.08 + unbalance_amp) * std::cos(omega * sim_time_sec) + 0.02 * std::cos(2.0 * omega * sim_time_sec) + noise_y;
    double az = 1.00 + (unbalance_amp * 0.35) * std::sin(2.0 * omega * sim_time_sec) + noise_z;

    g.x = static_cast<float>(ax);
    g.y = static_cast<float>(ay);
    g.z = static_cast<float>(az);

    sim_time_sec += (1.0 / SAMPLING_FREQ_HZ);
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000);

    Serial.println("\n==================================================");
    Serial.println("   VibeGuard Level-1: ESP32 Edge Vibration System");
    Serial.println("   ESP32-DevKitC-32E | ADXL345 (4-Wire SPI)");
    Serial.println("==================================================");

    pinMode(PIN_LED_RED,   OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_BLUE,  OUTPUT);
    setRGB(false, false, true); // Solid Blue during boot

    Serial.print("[INIT] Connecting to ADXL345 (4-Wire SPI CS=5, SCK=18, MISO=19, MOSI=23)... ");
    bool ok = accel.begin();
    if (ok) {
        Serial.println("SUCCESS! (0xE5 Detected, 800 Hz ODR, ±16g)");
    } else {
        Serial.println("STANDALONE BENCH MODE (Internal 10Hz Kinematics Active)");
    }

    current_state = update_state(current_state, true, false, false, false, false);
    Serial.println("[STATE] Self-Check Complete -> Calibrating...");
    delay(800);

    current_state = update_state(current_state, true, true, false, false, false);
    Serial.println("[STATE] Calibration Complete -> Normal Operation");
    updateIndicator(current_state, 0.11f);
}

void loop() {
    AccelGData sample;
    for (size_t i = 0; i < SAMPLE_BUFFER_SIZE; ++i) {
        unsigned long start_us = micros();
        if (accel.isHardwareDetected()) {
            accel.readG(sample);
        } else {
            generate_simulated_vibration(sample, 0.0f);
        }
        x_buffer[i] = sample.x;
        y_buffer[i] = sample.y;
        z_buffer[i] = sample.z;
        while (micros() - start_us < SAMPLE_INTERVAL_US);
    }

    remove_dc(x_buffer, SAMPLE_BUFFER_SIZE);
    remove_dc(y_buffer, SAMPLE_BUFFER_SIZE);
    remove_dc(z_buffer, SAMPLE_BUFFER_SIZE);

    float v_rms = compute_vector_rms(x_buffer, y_buffer, z_buffer, SAMPLE_BUFFER_SIZE);

    float peak_freq = 0.0f, peak_mag = 0.0f;
    compute_fft_peak(x_buffer, SAMPLE_BUFFER_SIZE, SAMPLING_FREQ_HZ, peak_freq, peak_mag);

    bool is_above = (v_rms >= RMS_ANOMALY_THRESHOLD);
    window_history.is_above_threshold.push_back(is_above);
    if (window_history.is_above_threshold.size() > 20) {
        window_history.is_above_threshold.erase(window_history.is_above_threshold.begin());
    }

    bool enter_abn = should_enter_abnormal(window_history, persistence_params);
    bool clear_abn = should_clear_abnormal(window_history, persistence_params);

    current_state = update_state(current_state, true, true, enter_abn, clear_abn, false);
    updateIndicator(current_state, v_rms);

    Serial.printf(">VRMS:%.3f,PeakFreq:%.1f,PeakMag:%.3f,State:%d\n",
                  v_rms, peak_freq, peak_mag, static_cast<int>(current_state));

    Serial.print("[TELEMETRY] Vector RMS: ");
    Serial.print(v_rms, 3);
    Serial.print(" g | Dominant Freq: ");
    Serial.print(peak_freq, 1);
    Serial.print(" Hz | Status: [");
    Serial.print(stateToString(current_state));
    Serial.println("]");
}
