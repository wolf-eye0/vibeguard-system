#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <cmath>
#include <cstring>
#include <cstddef>
#include "VibeGuardEngine.h"

// Global Deep Research DSP Engine
VibeGuardEngine g_vibeEngine;

// ============================================================================
// VIBEGUARD: BENCHMARK LOGGER + AUTOTUNING + SYNCHRONIZED MONITORING
// Base retained from VibeGuard_Benchmark_Logger-2.ino
// Hardware: 7Semi ESP32-DEVKIT-E + ADXL345 SPI
// SPI: CS=21, SCK=18, MISO=19, MOSI=23 (SPI MODE2 retained)
// RGB LED: Green=GPIO 25, Blue=GPIO 26, Red=GPIO 27
// DSP: 800 Hz ODR, +/-16 g full resolution, DC/gravity removal, 3D RMS
//
// IMPORTANT
// - The ESP32 is authoritative for NORMAL/WARNING/ALARM.
// - RGB and serial telemetry always use the same committed condition state.
// - Motor mass is profile metadata; measured healthy vibration sets thresholds.
// - Autotuning is locked during intentional imbalance testing.
// - Send commands with a newline (Serial Monitor: Newline or Both NL & CR).
// ============================================================================

#define PIN_CS 21
#define PIN_SCK 18
#define PIN_MISO 19
#define PIN_MOSI 23

#define PIN_LED_GREEN 25
#define PIN_LED_BLUE 26
#define PIN_LED_RED 27
#define PIN_BOOT_BUTTON 0

#define ADXL345_REG_DEVID 0x00
#define ADXL345_REG_BW_RATE 0x2C
#define ADXL345_REG_POWER_CTL 0x2D
#define ADXL345_REG_DATA_FORMAT 0x31
#define ADXL345_REG_DATAX0 0x32

#define ADXL345_DEVID 0xE5
#define SCALE_FACTOR 0.00390625f
#define SPI_SPEED 1000000
#define CHIP_SPI_MODE SPI_MODE2

// Kept as fallback values for the legacy benchmark table only.
// Live monitoring requires an approved autotuned profile.
#define RMS_WARN_LIMIT 0.35f
#define RMS_ALARM_LIMIT 0.70f

// 128-sample window (160 ms) covers 1.6 rotations at 600 RPM for smooth cycle integration.
#define SAMPLE_RATE_HZ 800
#define SAMPLE_PERIOD_US 1250UL
#define BUFFER_SIZE 128

#define PROFILE_MAGIC 0x56474233UL
#define PROFILE_SCHEMA 3
#define PROFILE_NAMESPACE "vgprof"
#define PROFILE_KEY "active"

#define STATIONARY_SECONDS 5
#define STATIONARY_WINDOWS ((STATIONARY_SECONDS * SAMPLE_RATE_HZ) / BUFFER_SIZE)
#define HEALTHY_RUN_SECONDS 15
#define HEALTHY_RUN_WINDOWS ((HEALTHY_RUN_SECONDS * SAMPLE_RATE_HZ) / BUFFER_SIZE)
#define REQUIRED_HEALTHY_RUNS 3
#define MAX_LEARN_WINDOWS (HEALTHY_RUN_WINDOWS * REQUIRED_HEALTHY_RUNS)

#define TELEMETRY_INTERVAL_MS 160UL
#define STATUS_INTERVAL_MS 2000UL
#define SENSOR_RETRY_MS 500UL
#define COMMAND_BUFFER_LIMIT 96

// Initial engineering settings. Validate these with repeated physical tests.
#define STATIONARY_MAX_MEDIAN_G 0.080f
#define STATIONARY_MAX_PEAK_WINDOW_G 0.180f
#define MIN_VALID_RATIO 0.90f
#define REPEATABILITY_REL_LIMIT 0.25f
#define REPEATABILITY_ABS_LIMIT_G 0.020f
#define WARN_K_SPREAD 4.0f
#define ALARM_K_SPREAD 12.0f
#define WARN_MIN_MARGIN_G 0.090f
#define ALARM_MIN_MARGIN_G 0.300f
#define WARN_NOISE_MULTIPLIER 2.0f
#define ALARM_NOISE_MULTIPLIER 4.0f
#define MIN_THRESHOLD_GAP_G 0.050f
#define WARN_EXIT_FRACTION 0.60f
#define ALARM_EXIT_FRACTION 0.75f
#define DEFAULT_WARN_PERSIST 4
#define DEFAULT_ALARM_PERSIST 4
#define DEFAULT_RECOVERY_PERSIST 6
#define INVALID_WINDOWS_FOR_FAULT 5
#define CLIP_LIMIT_G 15.5f

// ============================================================================
// TYPES
// ============================================================================

enum class OperatingMode : uint8_t {
  NO_PROFILE,
  MOTOR_OFF_REFERENCE,
  WAIT_HEALTHY_RUN,
  LEARNING_HEALTHY,
  AWAIT_APPROVAL,
  MONITORING,
  IMBALANCE_TEST,
  SENSOR_FAULT
};

enum class ConditionState : uint8_t {
  UNKNOWN,
  NORMAL,
  WARNING,
  ALARM,
  SENSOR_FAULT
};

struct WindowMetrics {
  float mean_x;
  float mean_y;
  float mean_z;
  float rms_x;
  float rms_y;
  float rms_z;
  float vector_rms;
  float peak_vector;
  float crest_factor;
  float unbalance_1x;
  float condition_vrms;
  float j_val;
  float j_smooth;
  float f1_hz;
  float c_sag;
  bool shock_rejected;
  uint8_t stage_4;
  bool valid;
  bool clipped;
};

struct ProfileData {
  uint32_t magic;
  uint16_t schema;
  char profile_id[16];
  float motor_mass_g;
  float stationary_mean_x;
  float stationary_mean_y;
  float stationary_mean_z;
  float stationary_noise_rms;
  float baseline_rms;
  float spread_mad_scaled;
  float warning_enter;
  float warning_exit;
  float alarm_enter;
  float alarm_exit;
  uint16_t sample_rate_hz;
  uint16_t window_samples;
  uint8_t warning_persist;
  uint8_t alarm_persist;
  uint8_t recovery_persist;
  uint8_t approved;
  uint32_t commissioned_ms;
  uint32_t crc32;
};

struct CandidateData {
  bool stationary_ready;
  bool valid;
  uint8_t healthy_runs;
  char profile_id[16];
  float motor_mass_g;
  float stationary_mean_x;
  float stationary_mean_y;
  float stationary_mean_z;
  float stationary_noise_rms;
  float baseline_rms;
  float spread_mad_scaled;
  float warning_enter;
  float warning_exit;
  float alarm_enter;
  float alarm_exit;
};

struct BenchmarkStep {
  const char* label;
  const char* mass_desc;
  float v_rms;
  bool recorded;
};

// ============================================================================
// GLOBALS
// ============================================================================

BenchmarkStep g_benchmarks[4] = {
  { "0. Baseline Run", "Bare Arm (0.0g)", 0.0f, false },
  { "1. Bolt Only", "M3x20 Bolt (~2.5g)", 0.0f, false },
  { "2. Bolt + 1 Nut", "Bolt + 1 Nut (~3.4g)", 0.0f, false },
  { "3. Bolt + 2 Nuts", "Bolt + 2 Nuts (~4.3g)", 0.0f, false }
};

Preferences g_preferences;
ProfileData g_profile{};
CandidateData g_candidate{};

// Rapid Field Calibration & Quadrature Tare globals
enum CalStreamingMode : int8_t {
  STREAM_CAL_IDLE = -1,
  STREAM_CAL_STAGE0 = 0,
  STREAM_CAL_STAGE1 = 1,
  STREAM_CAL_STAGE2 = 2,
  STREAM_CAL_TARE = 99
};

int8_t g_stream_cal_mode = STREAM_CAL_IDLE;
uint32_t g_stream_cal_start_ms = 0;
uint32_t g_stream_cal_count = 0;
double g_stream_cal_sum = 0.0;
float g_calibrated_stage_vrms[3] = {0.206f, 0.840f, 1.079f};
bool g_stage_calibrated[3] = {false, false, false};
float g_tare_floor_g = 0.0f;

float g_learn_values[MAX_LEARN_WINDOWS];
float g_scratch[MAX_LEARN_WINDOWS];
float g_run_medians[REQUIRED_HEALTHY_RUNS] = {0};
float g_run_spreads[REQUIRED_HEALTHY_RUNS] = {0};

bool g_sensor_ready = false;
bool g_forced_alarm = false;
OperatingMode g_mode = OperatingMode::NO_PROFILE;
OperatingMode g_mode_before_fault = OperatingMode::NO_PROFILE;
ConditionState g_committed_state = ConditionState::UNKNOWN;

uint8_t g_warning_count = 0;
uint8_t g_alarm_count = 0;
uint8_t g_recovery_count = 0;
uint8_t g_invalid_window_count = 0;
float g_smoothed_vrms = -1.0f;
uint32_t g_sequence = 0;
uint32_t g_last_telemetry_ms = 0;
uint32_t g_last_status_ms = 0;
uint32_t g_last_sensor_retry_ms = 0;
bool g_state_changed = false;
String g_command_buffer;
WindowMetrics g_last_metrics{};

char g_pending_profile_id[16] = "M01-A";
float g_pending_motor_mass_g = 0.0f;

// ============================================================================
// BASIC HELPERS
// ============================================================================

const char* modeName(OperatingMode mode) {
  switch (mode) {
    case OperatingMode::NO_PROFILE: return "NO_PROFILE";
    case OperatingMode::MOTOR_OFF_REFERENCE: return "MOTOR_OFF_REFERENCE";
    case OperatingMode::WAIT_HEALTHY_RUN: return "WAIT_HEALTHY_RUN";
    case OperatingMode::LEARNING_HEALTHY: return "LEARNING_HEALTHY";
    case OperatingMode::AWAIT_APPROVAL: return "AWAIT_APPROVAL";
    case OperatingMode::MONITORING: return "MONITORING";
    case OperatingMode::IMBALANCE_TEST: return "IMBALANCE_TEST";
    case OperatingMode::SENSOR_FAULT: return "SENSOR_FAULT";
    default: return "UNKNOWN_MODE";
  }
}

const char* conditionName(ConditionState state) {
  switch (state) {
    case ConditionState::NORMAL: return "NORMAL";
    case ConditionState::WARNING: return "WARNING";
    case ConditionState::ALARM: return "ALARM";
    case ConditionState::SENSOR_FAULT: return "SENSOR_FAULT";
    default: return "UNKNOWN";
  }
}

void setRGB(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_RED, r ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, g ? HIGH : LOW);
  digitalWrite(PIN_LED_BLUE, b ? HIGH : LOW);
}

void applyIndicator() {
  if (!g_sensor_ready || g_mode == OperatingMode::SENSOR_FAULT ||
      g_committed_state == ConditionState::SENSOR_FAULT) {
    setRGB(true, false, true);  // Solid Magenta: sensor/acquisition fault
    return;
  }

  // Active Field Calibration / Tare streaming recording:
  if (g_stream_cal_mode != STREAM_CAL_IDLE) {
    // Pulsing Purple (Red + Blue simultaneously at 4 Hz: 125 ms ON / 125 ms OFF)
    bool pulse = ((millis() / 125) % 2) == 0;
    setRGB(pulse, false, pulse);
    return;
  }

  if (g_vibeEngine.getCalibrationState() != CAL_IDLE) {
    setRGB(false, false, true); // Blue: 8.0s Median+MAD calibration active
    return;
  }

  if (g_mode == OperatingMode::NO_PROFILE ||
      g_mode == OperatingMode::MOTOR_OFF_REFERENCE ||
      g_mode == OperatingMode::WAIT_HEALTHY_RUN ||
      g_mode == OperatingMode::LEARNING_HEALTHY) {
    setRGB(false, false, true); // Blue: not classified / learning
    return;
  }

  if (g_mode == OperatingMode::AWAIT_APPROVAL) {
    setRGB(true, false, true);  // Purple: candidate awaits approval
    return;
  }

  switch (g_committed_state) {
    case ConditionState::ALARM: {
      // Fast Red Strobe (6.25 Hz: 80 ms ON / 80 ms OFF) for ALARM condition
      bool strobe = (millis() % 160) < 80;
      setRGB(strobe, false, false);
      break;
    }
    case ConditionState::WARNING: setRGB(true, true, false); break; // Solid Yellow
    case ConditionState::NORMAL:  setRGB(false, true, false); break; // Solid Green
    default: setRGB(false, false, true); break;
  }
}

void resetConditionCounters() {
  g_warning_count = 0;
  g_alarm_count = 0;
  g_recovery_count = 0;
  g_smoothed_vrms = -1.0f;
}

uint8_t incrementSaturated(uint8_t value, uint8_t max_val = 15) {
  return value < max_val ? value + 1 : max_val;
}

uint8_t decayCounter(uint8_t value) {
  return value > 0 ? value - 1 : 0;
}

float max3(float a, float b, float c) {
  return fmaxf(a, fmaxf(b, c));
}

void sortFloats(float* values, int count) {
  for (int i = 1; i < count; ++i) {
    float key = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
}

float medianCopy(const float* values, int count) {
  if (count <= 0 || count > MAX_LEARN_WINDOWS) return 0.0f;
  for (int i = 0; i < count; ++i) g_scratch[i] = values[i];
  sortFloats(g_scratch, count);
  if (count & 1) return g_scratch[count / 2];
  return 0.5f * (g_scratch[count / 2 - 1] + g_scratch[count / 2]);
}

float scaledMad(const float* values, int count, float median) {
  if (count <= 0 || count > MAX_LEARN_WINDOWS) return 0.0f;
  for (int i = 0; i < count; ++i) g_scratch[i] = fabsf(values[i] - median);
  sortFloats(g_scratch, count);
  float mad = (count & 1)
    ? g_scratch[count / 2]
    : 0.5f * (g_scratch[count / 2 - 1] + g_scratch[count / 2]);
  return 1.4826f * mad;
}

// ============================================================================
// PROFILE STORAGE
// ============================================================================

uint32_t crc32Bytes(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      uint32_t mask = -(crc & 1UL);
      crc = (crc >> 1) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

uint32_t profileCrc(const ProfileData& profile) {
  return crc32Bytes(reinterpret_cast<const uint8_t*>(&profile), offsetof(ProfileData, crc32));
}

bool profileStructValid(const ProfileData& profile) {
  if (profile.magic != PROFILE_MAGIC || profile.schema != PROFILE_SCHEMA || !profile.approved) return false;
  if (profile.sample_rate_hz != SAMPLE_RATE_HZ || profile.window_samples != BUFFER_SIZE) return false;
  if (!isfinite(profile.baseline_rms) || !isfinite(profile.warning_enter) || !isfinite(profile.alarm_enter)) return false;
  if (!(profile.warning_exit < profile.warning_enter &&
        profile.warning_enter < profile.alarm_enter &&
        profile.alarm_exit < profile.alarm_enter)) return false;
  return profile.crc32 == profileCrc(profile);
}

void clearProfileRam() {
  memset(&g_profile, 0, sizeof(g_profile));
  g_profile.magic = PROFILE_MAGIC;
  g_profile.schema = PROFILE_SCHEMA;
  g_profile.sample_rate_hz = SAMPLE_RATE_HZ;
  g_profile.window_samples = BUFFER_SIZE;
  g_profile.warning_persist = DEFAULT_WARN_PERSIST;
  g_profile.alarm_persist = DEFAULT_ALARM_PERSIST;
  g_profile.recovery_persist = DEFAULT_RECOVERY_PERSIST;
}

bool loadProfile() {
  clearProfileRam();
  if (!g_preferences.begin(PROFILE_NAMESPACE, true)) return false;
  size_t stored = g_preferences.getBytesLength(PROFILE_KEY);
  if (stored == sizeof(ProfileData)) {
    g_preferences.getBytes(PROFILE_KEY, &g_profile, sizeof(ProfileData));
  }
  g_tare_floor_g = g_preferences.getFloat("tare_floor", 0.0f);
  g_calibrated_stage_vrms[0] = g_preferences.getFloat("s0_vrms", 0.206f);
  g_calibrated_stage_vrms[1] = g_preferences.getFloat("s1_vrms", 0.840f);
  g_calibrated_stage_vrms[2] = g_preferences.getFloat("s2_vrms", 1.079f);
  g_stage_calibrated[0] = g_preferences.getBool("s0_cal", false);
  g_stage_calibrated[1] = g_preferences.getBool("s1_cal", false);
  g_stage_calibrated[2] = g_preferences.getBool("s2_cal", false);
  g_preferences.end();

  if (!profileStructValid(g_profile)) {
    clearProfileRam();
    return false;
  }

  strlcpy(g_pending_profile_id, g_profile.profile_id, sizeof(g_pending_profile_id));
  g_pending_motor_mass_g = g_profile.motor_mass_g;
  g_profile.warning_persist = DEFAULT_WARN_PERSIST;
  g_profile.alarm_persist = DEFAULT_ALARM_PERSIST;
  g_profile.recovery_persist = DEFAULT_RECOVERY_PERSIST;
  return true;
}

bool saveProfile() {
  g_profile.crc32 = profileCrc(g_profile);
  if (!g_preferences.begin(PROFILE_NAMESPACE, false)) return false;
  size_t written = g_preferences.putBytes(PROFILE_KEY, &g_profile, sizeof(ProfileData));
  g_preferences.putFloat("tare_floor", g_tare_floor_g);
  g_preferences.putFloat("s0_vrms", g_calibrated_stage_vrms[0]);
  g_preferences.putFloat("s1_vrms", g_calibrated_stage_vrms[1]);
  g_preferences.putFloat("s2_vrms", g_calibrated_stage_vrms[2]);
  g_preferences.putBool("s0_cal", g_stage_calibrated[0]);
  g_preferences.putBool("s1_cal", g_stage_calibrated[1]);
  g_preferences.putBool("s2_cal", g_stage_calibrated[2]);
  g_preferences.end();
  return written == sizeof(ProfileData);
}

void eraseSavedProfile() {
  if (g_preferences.begin(PROFILE_NAMESPACE, false)) {
    g_preferences.remove(PROFILE_KEY);
    g_preferences.remove("tare_floor");
    g_preferences.remove("s0_vrms");
    g_preferences.remove("s1_vrms");
    g_preferences.remove("s2_vrms");
    g_preferences.remove("s0_cal");
    g_preferences.remove("s1_cal");
    g_preferences.remove("s2_cal");
    g_preferences.end();
  }
  clearProfileRam();
  g_tare_floor_g = 0.0f;
  g_stage_calibrated[0] = false;
  g_stage_calibrated[1] = false;
  g_stage_calibrated[2] = false;
  memset(&g_candidate, 0, sizeof(g_candidate));
  resetConditionCounters();
  g_mode = OperatingMode::NO_PROFILE;
  g_committed_state = ConditionState::UNKNOWN;
  g_state_changed = true;
  applyIndicator();
  Serial.println("[COMMAND] ack=PROFILE_CLEAR ok=1");
}

// ============================================================================
// ADXL345 SPI
// ============================================================================

uint8_t readReg(uint8_t reg) {
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, CHIP_SPI_MODE));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(5);
  SPI.transfer(reg | 0x80);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  delayMicroseconds(10);
  return value;
}

void writeReg(uint8_t reg, uint8_t value) {
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, CHIP_SPI_MODE));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(5);
  SPI.transfer(reg & 0x7F);
  SPI.transfer(value);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  delayMicroseconds(10);
}

void readAccelBurst(float& ax, float& ay, float& az) {
  uint8_t raw[6];
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, CHIP_SPI_MODE));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(5);
  SPI.transfer(ADXL345_REG_DATAX0 | 0x80 | 0x40);
  for (int i = 0; i < 6; ++i) raw[i] = SPI.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();

  int16_t x = static_cast<int16_t>(raw[0] | (raw[1] << 8));
  int16_t y = static_cast<int16_t>(raw[2] | (raw[3] << 8));
  int16_t z = static_cast<int16_t>(raw[4] | (raw[5] << 8));
  ax = static_cast<float>(x) * SCALE_FACTOR;
  ay = static_cast<float>(y) * SCALE_FACTOR;
  az = static_cast<float>(z) * SCALE_FACTOR;
}

bool initializeSensor() {
  uint8_t id = readReg(ADXL345_REG_DEVID);
  if (id != ADXL345_DEVID) return false;

  writeReg(ADXL345_REG_POWER_CTL, 0x00);
  delay(10);
  writeReg(ADXL345_REG_BW_RATE, 0x0D);     // 800 Hz ODR
  delay(5);
  writeReg(ADXL345_REG_DATA_FORMAT, 0x0B); // Full resolution, +/-16 g
  delay(5);
  writeReg(ADXL345_REG_POWER_CTL, 0x08);   // Measurement mode
  delay(20);
  return true;
}

WindowMetrics sampleWindowMetrics() {
  static float x[BUFFER_SIZE];
  static float y[BUFFER_SIZE];
  static float z[BUFFER_SIZE];
  WindowMetrics out{};

  for (int i = 0; i < BUFFER_SIZE; ++i) {
    uint32_t started = micros();
    readAccelBurst(x[i], y[i], z[i]);

    if (!isfinite(x[i]) || !isfinite(y[i]) || !isfinite(z[i])) return out;
    if (fabsf(x[i]) >= CLIP_LIMIT_G || fabsf(y[i]) >= CLIP_LIMIT_G || fabsf(z[i]) >= CLIP_LIMIT_G) {
      out.clipped = true;
    }

    out.mean_x += x[i];
    out.mean_y += y[i];
    out.mean_z += z[i];
    if ((i & 0x0F) == 0 && g_committed_state == ConditionState::ALARM) {
      applyIndicator();
    }
    while (static_cast<uint32_t>(micros() - started) < SAMPLE_PERIOD_US) { yield(); }
  }

  out.mean_x /= BUFFER_SIZE;
  out.mean_y /= BUFFER_SIZE;
  out.mean_z /= BUFFER_SIZE;

  float static_g = sqrtf(out.mean_x * out.mean_x + out.mean_y * out.mean_y + out.mean_z * out.mean_z);
  if (static_g < 0.10f || static_g > 3.0f) {
    // Window has abnormal DC offset or incomplete cycle integration under extreme vibration.
    // Mark this window invalid; DO NOT immediately declare hardware disconnection.
    // Persistent faults are debounced by g_invalid_window_count >= INVALID_WINDOWS_FOR_FAULT.
    out.valid = false;
    return out;
  }

  float sum_x = 0.0f, sum_y = 0.0f, sum_z = 0.0f;
  float peak_sq = 0.0f;
  for (int i = 0; i < BUFFER_SIZE; ++i) {
    float dx = x[i] - out.mean_x;
    float dy = y[i] - out.mean_y;
    float dz = z[i] - out.mean_z;
    float px = dx * dx;
    float py = dy * dy;
    float pz = dz * dz;
    float vector_sq = px + py + pz;
    sum_x += px;
    sum_y += py;
    sum_z += pz;
    if (vector_sq > peak_sq) peak_sq = vector_sq;
  }

  out.rms_x = sqrtf(sum_x / BUFFER_SIZE);
  out.rms_y = sqrtf(sum_y / BUFFER_SIZE);
  out.rms_z = sqrtf(sum_z / BUFFER_SIZE);
  out.vector_rms = sqrtf(out.rms_x * out.rms_x + out.rms_y * out.rms_y + out.rms_z * out.rms_z);
  out.peak_vector = sqrtf(peak_sq);
  out.crest_factor = out.vector_rms > 0.0001f ? out.peak_vector / out.vector_rms : NAN;

  // Pass raw window buffer to Deep Research Substrate-Invariant DSP Pipeline
  TelemetryFrame tf = g_vibeEngine.processWindow(x, y, z, g_preferences);

  out.rms_x = tf.rx;
  out.rms_y = tf.ry;
  out.rms_z = tf.rz;
  out.vector_rms = tf.r3d;
  out.peak_vector = tf.peakVector;
  out.crest_factor = tf.crestFactor;
  out.unbalance_1x = tf.rLowXY;
  out.j_val = tf.rawJ;
  out.j_smooth = tf.smoothedJ;
  out.f1_hz = tf.f1Hz;
  out.c_sag = tf.cSag;
  out.shock_rejected = tf.shockRejected;
  out.stage_4 = static_cast<uint8_t>(tf.committedStage);
  out.condition_vrms = tf.r3d;
  out.valid = !out.clipped && isfinite(out.vector_rms);

  // Quadrature Tare (Root-Difference-of-Squares: V_net = sqrt(V_raw^2 - V_floor^2))
  if (out.valid && g_tare_floor_g > 0.0005f) {
    float raw_sq = out.vector_rms * out.vector_rms;
    float tare_sq = g_tare_floor_g * g_tare_floor_g;
    float net_sq = raw_sq - tare_sq;
    out.vector_rms = (net_sq > 0.0f) ? sqrtf(net_sq) : 0.0f;
    out.condition_vrms = out.vector_rms;
  }

  // Active Streaming Field Calibration Accumulator
  if (out.valid && g_stream_cal_mode != STREAM_CAL_IDLE) {
    g_stream_cal_count++;
    if (g_stream_cal_mode == STREAM_CAL_TARE) {
      g_stream_cal_sum += tf.r3d; // Raw floor
    } else {
      g_stream_cal_sum += out.vector_rms; // Net RMS
    }
  }

  return out;
}

float sample_window_rms() {
  WindowMetrics metrics = sampleWindowMetrics();
  return metrics.valid ? metrics.vector_rms : -1.0f;
}

// ============================================================================
// SYNCHRONIZED CONDITION ENGINE
// ============================================================================

void commitState(ConditionState next) {
  if (next != g_committed_state) {
    Serial.printf("[EVENT] type=state_change from=%s to=%s device_ms=%lu\n",
                  conditionName(g_committed_state), conditionName(next), millis());
    g_committed_state = next;
    g_state_changed = true;
  }
  applyIndicator();
}

ConditionState evaluateCondition(const WindowMetrics& m, bool valid) {
  if (!g_sensor_ready || g_invalid_window_count >= INVALID_WINDOWS_FOR_FAULT) {
    return ConditionState::SENSOR_FAULT;
  }

  if (g_forced_alarm) return ConditionState::ALARM;

  bool activeMonitoring = g_mode == OperatingMode::MONITORING ||
                          g_mode == OperatingMode::IMBALANCE_TEST;
  if (!activeMonitoring || !g_profile.approved) return ConditionState::UNKNOWN;
  if (!valid) return g_committed_state;

  // Net vibration magnitude (after Tare floor removal)
  float vrms = m.condition_vrms;

  // Dual-Criterion State Evaluation:
  // 1. Primary: Calibrated Threshold Corridors (saved in Flash)
  // 2. Secondary: Harmonic Substrate-Invariant Stage (if stage_4 >= 1)
  bool alarmEvidence   = (vrms >= g_profile.alarm_enter)   || (m.stage_4 >= 2);
  bool warningEvidence = (vrms >= g_profile.warning_enter) || (m.stage_4 == 1);
  bool alarmReleased   = (vrms < g_profile.alarm_exit)     && (m.stage_4 < 2);
  bool warningReleased = (vrms < g_profile.warning_exit)   && (m.stage_4 == 0);

  g_alarm_count   = alarmEvidence   ? incrementSaturated(g_alarm_count)   : decayCounter(g_alarm_count);
  g_warning_count = warningEvidence ? incrementSaturated(g_warning_count) : decayCounter(g_warning_count);

  switch (g_committed_state) {
    case ConditionState::NORMAL:
      g_recovery_count = 0;
      if (g_alarm_count >= g_profile.alarm_persist) return ConditionState::ALARM;
      if (g_warning_count >= g_profile.warning_persist) return ConditionState::WARNING;
      return ConditionState::NORMAL;

    case ConditionState::WARNING:
      if (g_alarm_count >= g_profile.alarm_persist) {
        g_recovery_count = 0;
        return ConditionState::ALARM;
      }
      if (warningReleased) {
        g_recovery_count = incrementSaturated(g_recovery_count);
        if (g_recovery_count >= g_profile.recovery_persist) {
          g_recovery_count = 0;
          return ConditionState::NORMAL;
        }
      } else {
        g_recovery_count = 0;
      }
      return ConditionState::WARNING;

    case ConditionState::ALARM:
      if (alarmReleased) {
        g_recovery_count = incrementSaturated(g_recovery_count);
        if (g_recovery_count >= g_profile.recovery_persist) {
          g_recovery_count = 0;
          return warningEvidence ? ConditionState::WARNING : ConditionState::NORMAL;
        }
      } else {
        g_recovery_count = 0;
      }
      return ConditionState::ALARM;

    default:
      return ConditionState::NORMAL;
  }
}



// ============================================================================
// AUTOTUNING
// ============================================================================

void resetCandidate(bool keepMetadata = true) {
  char savedId[16];
  float savedMass = g_pending_motor_mass_g;
  strlcpy(savedId, g_pending_profile_id, sizeof(savedId));
  memset(&g_candidate, 0, sizeof(g_candidate));
  memset(g_run_medians, 0, sizeof(g_run_medians));
  memset(g_run_spreads, 0, sizeof(g_run_spreads));
  if (keepMetadata) {
    strlcpy(g_candidate.profile_id, savedId, sizeof(g_candidate.profile_id));
    g_candidate.motor_mass_g = savedMass;
  }
}

bool collectStationaryReference() {
  if (g_mode == OperatingMode::IMBALANCE_TEST) {
    Serial.println("[COMMAND] ack=CAL_OFF ok=0 reason=AUTOTUNE_LOCKED_IN_TEST_MODE");
    return false;
  }

  resetCandidate(true);
  g_mode = OperatingMode::MOTOR_OFF_REFERENCE;
  commitState(ConditionState::UNKNOWN);
  Serial.printf("[CAL] stage=MOTOR_OFF_REFERENCE windows=%d instruction=MOTOR_OFF_NO_TEST_MASS\n", STATIONARY_WINDOWS);

  float values[STATIONARY_WINDOWS];
  float meanX = 0, meanY = 0, meanZ = 0;
  float maxWindow = 0;
  int valid = 0;

  for (int i = 0; i < STATIONARY_WINDOWS; ++i) {
    WindowMetrics m = sampleWindowMetrics();
    if (m.valid) {
      values[valid++] = m.vector_rms;
      meanX += m.mean_x;
      meanY += m.mean_y;
      meanZ += m.mean_z;
      maxWindow = fmaxf(maxWindow, m.vector_rms);
    }
    if ((i % 8) == 0 || i == STATIONARY_WINDOWS - 1) {
      Serial.printf("[CAL] stage=MOTOR_OFF_REFERENCE progress=%.2f valid=%d\n",
                    static_cast<float>(i + 1) / STATIONARY_WINDOWS, valid);
    }
    if (!g_sensor_ready) break;
  }

  float validRatio = static_cast<float>(valid) / STATIONARY_WINDOWS;
  if (!g_sensor_ready || valid == 0 || validRatio < MIN_VALID_RATIO) {
    g_mode = g_profile.approved ? OperatingMode::MONITORING : OperatingMode::NO_PROFILE;
    Serial.println("[CAL] stage=MOTOR_OFF_REFERENCE result=REJECT reason=INVALID_WINDOWS");
    applyIndicator();
    return false;
  }

  float noise = medianCopy(values, valid);
  if (noise > STATIONARY_MAX_MEDIAN_G || maxWindow > STATIONARY_MAX_PEAK_WINDOW_G) {
    g_mode = g_profile.approved ? OperatingMode::MONITORING : OperatingMode::NO_PROFILE;
    Serial.printf("[CAL] stage=MOTOR_OFF_REFERENCE result=REJECT reason=RIG_NOT_STATIONARY median=%.4f max=%.4f\n",
                  noise, maxWindow);
    applyIndicator();
    return false;
  }

  g_candidate.stationary_ready = true;
  g_candidate.stationary_mean_x = meanX / valid;
  g_candidate.stationary_mean_y = meanY / valid;
  g_candidate.stationary_mean_z = meanZ / valid;
  g_candidate.stationary_noise_rms = noise;
  g_mode = OperatingMode::WAIT_HEALTHY_RUN;
  applyIndicator();
  Serial.printf("[CAL] stage=MOTOR_OFF_REFERENCE result=PASS noise_rms=%.5f next=CAL_RUN\n", noise);
  return true;
}

bool deriveCandidateThresholds() {
  int total = HEALTHY_RUN_WINDOWS * REQUIRED_HEALTHY_RUNS;
  float minRun = g_run_medians[0];
  float maxRun = g_run_medians[0];
  float runCenter = 0.0f;
  for (int i = 0; i < REQUIRED_HEALTHY_RUNS; ++i) {
    minRun = fminf(minRun, g_run_medians[i]);
    maxRun = fmaxf(maxRun, g_run_medians[i]);
    runCenter += g_run_medians[i];
  }
  runCenter /= REQUIRED_HEALTHY_RUNS;
  float allowedDifference = fmaxf(REPEATABILITY_ABS_LIMIT_G, runCenter * REPEATABILITY_REL_LIMIT);

  if ((maxRun - minRun) > allowedDifference) {
    Serial.printf("[CAL] stage=DERIVE result=REJECT reason=POOR_REPEATABILITY spread_between_runs=%.5f allowed=%.5f\n",
                  maxRun - minRun, allowedDifference);
    g_candidate.healthy_runs = 0;
    g_candidate.valid = false;
    g_mode = OperatingMode::WAIT_HEALTHY_RUN;
    return false;
  }

  float baseline = medianCopy(g_learn_values, total);
  float spread = scaledMad(g_learn_values, total, baseline);
  float noise = g_candidate.stationary_noise_rms;

  float warningMargin = max3(WARN_K_SPREAD * spread, WARN_MIN_MARGIN_G,
                             WARN_NOISE_MULTIPLIER * noise);
  float alarmMargin = max3(ALARM_K_SPREAD * spread, ALARM_MIN_MARGIN_G,
                           ALARM_NOISE_MULTIPLIER * noise);

  g_candidate.baseline_rms = baseline;
  g_candidate.spread_mad_scaled = spread;
  g_candidate.warning_enter = baseline + warningMargin;
  g_candidate.alarm_enter = fmaxf(baseline + alarmMargin,
                                  g_candidate.warning_enter + MIN_THRESHOLD_GAP_G);
  g_candidate.warning_exit = baseline + WARN_EXIT_FRACTION *
                             (g_candidate.warning_enter - baseline);
  g_candidate.alarm_exit = g_candidate.warning_enter + ALARM_EXIT_FRACTION *
                           (g_candidate.alarm_enter - g_candidate.warning_enter);
  g_candidate.valid = true;
  g_mode = OperatingMode::AWAIT_APPROVAL;
  commitState(ConditionState::UNKNOWN);

  Serial.printf("[CAL] stage=CANDIDATE result=READY profile=%s mass_g=%.2f baseline=%.5f spread=%.5f warn_enter=%.5f warn_exit=%.5f alarm_enter=%.5f alarm_exit=%.5f approval_required=1\n",
                g_candidate.profile_id, g_candidate.motor_mass_g,
                g_candidate.baseline_rms, g_candidate.spread_mad_scaled,
                g_candidate.warning_enter, g_candidate.warning_exit,
                g_candidate.alarm_enter, g_candidate.alarm_exit);
  return true;
}

bool collectHealthyRun() {
  if (g_mode == OperatingMode::IMBALANCE_TEST) {
    Serial.println("[COMMAND] ack=CAL_RUN ok=0 reason=AUTOTUNE_LOCKED_IN_TEST_MODE");
    return false;
  }
  if (!g_candidate.stationary_ready) {
    Serial.println("[COMMAND] ack=CAL_RUN ok=0 reason=RUN_CAL_OFF_FIRST");
    return false;
  }
  if (g_candidate.healthy_runs >= REQUIRED_HEALTHY_RUNS) {
    Serial.println("[COMMAND] ack=CAL_RUN ok=0 reason=RUNS_ALREADY_COMPLETE");
    return false;
  }

  int run = g_candidate.healthy_runs;
  int offset = run * HEALTHY_RUN_WINDOWS;
  int valid = 0;
  g_mode = OperatingMode::LEARNING_HEALTHY;
  commitState(ConditionState::UNKNOWN);
  Serial.printf("[CAL] stage=HEALTHY_RUN run=%d/%d windows=%d instruction=KNOWN_HEALTHY_NORMAL_CONFIGURATION\n",
                run + 1, REQUIRED_HEALTHY_RUNS, HEALTHY_RUN_WINDOWS);

  for (int i = 0; i < HEALTHY_RUN_WINDOWS; ++i) {
    WindowMetrics m = sampleWindowMetrics();
    if (m.valid) g_learn_values[offset + valid++] = m.vector_rms;
    if ((i % 20) == 0 || i == HEALTHY_RUN_WINDOWS - 1) {
      Serial.printf("[CAL] stage=HEALTHY_RUN run=%d progress=%.2f valid=%d\n",
                    run + 1, static_cast<float>(i + 1) / HEALTHY_RUN_WINDOWS, valid);
    }
    if (!g_sensor_ready) break;
  }

  float validRatio = static_cast<float>(valid) / HEALTHY_RUN_WINDOWS;
  if (!g_sensor_ready || validRatio < MIN_VALID_RATIO) {
    g_mode = OperatingMode::WAIT_HEALTHY_RUN;
    Serial.printf("[CAL] stage=HEALTHY_RUN run=%d result=REJECT reason=INVALID_WINDOWS\n", run + 1);
    applyIndicator();
    return false;
  }

  // Compact valid data to a full run segment only when enough windows exist.
  // Missing tail values are filled with the run median so aggregate indexing remains fixed.
  float runMedian = medianCopy(&g_learn_values[offset], valid);
  float runSpread = scaledMad(&g_learn_values[offset], valid, runMedian);
  for (int i = valid; i < HEALTHY_RUN_WINDOWS; ++i) g_learn_values[offset + i] = runMedian;
  g_run_medians[run] = runMedian;
  g_run_spreads[run] = runSpread;
  g_candidate.healthy_runs++;

  Serial.printf("[CAL] stage=HEALTHY_RUN run=%d result=PASS median=%.5f spread=%.5f completed=%d/%d\n",
                run + 1, runMedian, runSpread, g_candidate.healthy_runs, REQUIRED_HEALTHY_RUNS);

  if (g_candidate.healthy_runs == REQUIRED_HEALTHY_RUNS) {
    return deriveCandidateThresholds();
  }

  g_mode = OperatingMode::WAIT_HEALTHY_RUN;
  applyIndicator();
  Serial.println("[CAL] next=CAL_RUN instruction=STOP_CHECK_RESTART_NORMAL_CONFIGURATION");
  return true;
}

bool approveCandidate() {
  if (!g_candidate.valid || g_mode != OperatingMode::AWAIT_APPROVAL) {
    Serial.println("[COMMAND] ack=PROFILE_APPROVE ok=0 reason=NO_VALID_CANDIDATE");
    return false;
  }

  ProfileData next{};
  next.magic = PROFILE_MAGIC;
  next.schema = PROFILE_SCHEMA;
  strlcpy(next.profile_id, g_candidate.profile_id, sizeof(next.profile_id));
  next.motor_mass_g = g_candidate.motor_mass_g;
  next.stationary_mean_x = g_candidate.stationary_mean_x;
  next.stationary_mean_y = g_candidate.stationary_mean_y;
  next.stationary_mean_z = g_candidate.stationary_mean_z;
  next.stationary_noise_rms = g_candidate.stationary_noise_rms;
  next.baseline_rms = g_candidate.baseline_rms;
  next.spread_mad_scaled = g_candidate.spread_mad_scaled;
  next.warning_enter = g_candidate.warning_enter;
  next.warning_exit = g_candidate.warning_exit;
  next.alarm_enter = g_candidate.alarm_enter;
  next.alarm_exit = g_candidate.alarm_exit;
  next.sample_rate_hz = SAMPLE_RATE_HZ;
  next.window_samples = BUFFER_SIZE;
  next.warning_persist = DEFAULT_WARN_PERSIST;
  next.alarm_persist = DEFAULT_ALARM_PERSIST;
  next.recovery_persist = DEFAULT_RECOVERY_PERSIST;
  next.approved = 1;
  next.commissioned_ms = millis();
  next.crc32 = profileCrc(next);

  ProfileData previous = g_profile;
  g_profile = next;
  if (!saveProfile()) {
    g_profile = previous;
    Serial.println("[COMMAND] ack=PROFILE_APPROVE ok=0 reason=NVS_WRITE_FAILED");
    return false;
  }

  g_mode = OperatingMode::MONITORING;
  g_forced_alarm = false;
  resetConditionCounters();
  commitState(ConditionState::NORMAL);
  Serial.printf("[COMMAND] ack=PROFILE_APPROVE ok=1 profile=%s baseline=%.5f warn=%.5f alarm=%.5f\n",
                g_profile.profile_id, g_profile.baseline_rms,
                g_profile.warning_enter, g_profile.alarm_enter);
  return true;
}

void cancelCalibration() {
  resetCandidate(true);
  g_mode = g_profile.approved ? OperatingMode::MONITORING : OperatingMode::NO_PROFILE;
  resetConditionCounters();
  commitState(g_profile.approved ? ConditionState::NORMAL : ConditionState::UNKNOWN);
  Serial.println("[COMMAND] ack=CAL_CANCEL ok=1 previous_profile_preserved=1");
}

// ============================================================================
// LEGACY BENCHMARK LOGGER (PRESERVED)
// ============================================================================

float collectAveragedRms(int numWindows = 24) {
  setRGB(false, false, true);
  float total = 0.0f;
  int valid = 0;
  for (int i = 0; i < numWindows; ++i) {
    float value = sample_window_rms();
    if (value >= 0.0f) {
      total += value;
      valid++;
    }
    Serial.print('.');
    delay(5);
  }
  Serial.println();
  applyIndicator();
  return valid > 0 ? total / valid : -1.0f;
}

float benchmarkWarnLimit() {
  return g_profile.approved ? g_profile.warning_enter : RMS_WARN_LIMIT;
}

float benchmarkAlarmLimit() {
  return g_profile.approved ? g_profile.alarm_enter : RMS_ALARM_LIMIT;
}

void recordStage(int stageIndex) {
  if (stageIndex < 0 || stageIndex > 3) return;
  Serial.printf("\n>>> RECORDING: %s (%s) <<<\n",
                g_benchmarks[stageIndex].label, g_benchmarks[stageIndex].mass_desc);
  Serial.println("Sampling 24 windows. Keep the rig in the selected test condition.");

  float average = collectAveragedRms(24);
  if (average < 0.0f) {
    Serial.println("[BENCHMARK] result=REJECT reason=NO_VALID_DATA");
    return;
  }

  g_benchmarks[stageIndex].v_rms = average;
  g_benchmarks[stageIndex].recorded = true;
  float baseline = g_benchmarks[0].v_rms;
  float delta = g_benchmarks[0].recorded ? average - baseline : 0.0f;
  float percent = (g_benchmarks[0].recorded && baseline > 0.001f)
    ? 100.0f * delta / baseline : 0.0f;
  const char* status = average >= benchmarkAlarmLimit() ? "ALARM" :
                       average >= benchmarkWarnLimit() ? "WARNING" : "NORMAL";

  Serial.println("------------------------------------------------------------------");
  Serial.printf("[SUCCESS] Logged %s\n", g_benchmarks[stageIndex].label);
  Serial.printf("Measured Vector RMS: %.5f g\n", average);
  if (stageIndex > 0 && g_benchmarks[0].recorded) {
    Serial.printf("Increase Over Baseline: %+.5f g (%+.1f%%)\n", delta, percent);
  }
  Serial.printf("Benchmark Classification: %s\n", status);
  Serial.println("NOTE: Benchmark logging never changes the approved autotune profile.");
  Serial.println("------------------------------------------------------------------");
  applyIndicator();
}

void printSummaryTable() {
  Serial.println("\n================================================================================================");
  Serial.println("VIBEGUARD EXPERIMENTAL UNBALANCE BENCHMARK REPORT");
  Serial.println("================================================================================================");
  Serial.println("| Stage                   | Unbalance Mass       | Vector RMS | Delta RMS  | Increase | Status  |");
  Serial.println("|-------------------------|----------------------|------------|------------|----------|---------|");
  float baseline = g_benchmarks[0].recorded ? g_benchmarks[0].v_rms : 0.0f;

  for (int i = 0; i < 4; ++i) {
    if (!g_benchmarks[i].recorded) {
      Serial.printf("| %-23s | %-20s | NOT LOGGED | --         | --       | PENDING |\n",
                    g_benchmarks[i].label, g_benchmarks[i].mass_desc);
      continue;
    }
    float value = g_benchmarks[i].v_rms;
    float delta = (i == 0 || !g_benchmarks[0].recorded) ? 0.0f : value - baseline;
    float percent = (i == 0 || baseline <= 0.001f) ? 0.0f : 100.0f * delta / baseline;
    const char* status = value >= benchmarkAlarmLimit() ? "ALARM" :
                         value >= benchmarkWarnLimit() ? "WARNING" : "NORMAL";
    Serial.printf("| %-23s | %-20s | %8.4f g | %+8.4f g | %+7.1f%% | %-7s |\n",
                  g_benchmarks[i].label, g_benchmarks[i].mass_desc,
                  value, delta, percent, status);
  }
  Serial.println("================================================================================================");
  Serial.println("Thresholds shown use the approved profile when available; otherwise legacy fallback limits.\n");
}

// ============================================================================
// TELEMETRY
// ============================================================================

void printStatus() {
  Serial.printf("[STATUS] schema=3 mode=%s sensor=%s spi_mode=2 odr_hz=%d window=%d profile=%s approved=%d mass_g=%.2f baseline=%.5f spread=%.5f warn_enter=%.5f warn_exit=%.5f alarm_enter=%.5f alarm_exit=%.5f forced=%d autotune_locked=%d invalid_windows=%u\n",
                modeName(g_mode), g_sensor_ready ? "OK" : "FAULT",
                SAMPLE_RATE_HZ, BUFFER_SIZE,
                g_profile.approved ? g_profile.profile_id : g_pending_profile_id,
                g_profile.approved, g_profile.approved ? g_profile.motor_mass_g : g_pending_motor_mass_g,
                g_profile.baseline_rms, g_profile.spread_mad_scaled,
                g_profile.warning_enter, g_profile.warning_exit,
                g_profile.alarm_enter, g_profile.alarm_exit,
                g_forced_alarm, g_mode == OperatingMode::IMBALANCE_TEST,
                g_invalid_window_count);
}

void printTelemetry(const WindowMetrics& m) {
  float baseline = g_profile.approved ? g_profile.baseline_rms : 0.0f;
  float warning = g_profile.approved ? g_profile.warning_enter : RMS_WARN_LIMIT;
  float alarm = g_profile.approved ? g_profile.alarm_enter : RMS_ALARM_LIMIT;
  const char* state = conditionName(g_committed_state);

  // Plotter-style line (compatible with server.js & web dashboard)
  Serial.printf(">VRMS:%.5f,Baseline:%.5f,WarnLimit:%.5f,AlarmLimit:%.5f,State:%s,J:%.4f,f1:%.2f,Stage:%u,Shock:%d,Tare:%.5f\n",
                m.condition_vrms, baseline, warning, alarm, state,
                m.j_smooth, m.f1_hz, m.stage_4, m.shock_rejected ? 1 : 0, g_tare_floor_g);

  // Human-readable live line
  Serial.printf("[LIVE] VRMS: %7.5f g | J(t): %6.2f | Stage: %u | Condition: [%-12s] | Mode: %-20s\n",
                m.condition_vrms, m.j_smooth, m.stage_4, state, modeName(g_mode));

  // Machine-readable tagged telemetry for bridge.py / server.js
  float cur_cal_avg = (g_stream_cal_count > 0) ? static_cast<float>(g_stream_cal_sum / g_stream_cal_count) : 0.0f;
  Serial.printf("[DATA] schema=3 seq=%lu device_ms=%lu mode=%s state=%s stage4=%u J=%.4f f1=%.2f cSag=%.2f shock=%d forced=%d profile=%s approved=%d mass_g=%.2f mean=(%.5f,%.5f,%.5f) rms=(%.5f,%.5f,%.5f) vector=%.5f peak=%.5f crest=%.4f baseline=%.5f warning=%.5f alarm=%.5f valid=%d clipped=%d tare=%.5f calMode=%d calSec=%.1f calCount=%u calAvg=%.5f\n",
                ++g_sequence, millis(), modeName(g_mode), state, m.stage_4,
                m.j_smooth, m.f1_hz, m.c_sag, m.shock_rejected ? 1 : 0,
                g_forced_alarm,
                g_profile.approved ? g_profile.profile_id : g_pending_profile_id,
                g_profile.approved, g_profile.approved ? g_profile.motor_mass_g : g_pending_motor_mass_g,
                m.mean_x, m.mean_y, m.mean_z,
                m.rms_x, m.rms_y, m.rms_z,
                m.vector_rms, m.peak_vector, m.crest_factor,
                baseline, warning, alarm, m.valid, m.clipped,
                g_tare_floor_g,
                static_cast<int>(g_stream_cal_mode),
                (g_stream_cal_mode != STREAM_CAL_IDLE) ? ((millis() - g_stream_cal_start_ms) / 1000.0f) : 0.0f,
                g_stream_cal_count,
                cur_cal_avg);
}

// ============================================================================
// COMMANDS
// ============================================================================

void printHelpMenu() {
  Serial.println("\n==========================================================================");
  Serial.println("VIBEGUARD COMMAND MENU (send with newline)");
  Serial.println("==========================================================================");
  Serial.println("Legacy benchmark commands:");
  Serial.println("  0 or b        Record normal/bare-arm benchmark");
  Serial.println("  1 / 2 / 3     Record controlled imbalance benchmark stages");
  Serial.println("  s or t        Print benchmark summary");
  Serial.println("  c             Clear benchmark ledger only");
  Serial.println("  ? or h        Show this menu");
  Serial.println();
  Serial.println("Autotuning/profile commands:");
  Serial.println("  NAME=M01-A        Set pending profile ID (max 15 characters)");
  Serial.println("  MASS=320.0        Set motor mass metadata in grams");
  Serial.println("  CAL_OFF           Capture 5 s motor-off stationary reference");
  Serial.println("  CAL_RUN           Capture one 15 s known-healthy run; repeat 3 times");
  Serial.println("  PROFILE_APPROVE   Approve and save the generated candidate");
  Serial.println("  CAL_CANCEL        Cancel tuning; preserve old approved profile");
  Serial.println("  PROFILE_CLEAR     Erase the approved profile");
  Serial.println();
  Serial.println("Monitoring/test commands:");
  Serial.println("  STATUS            Print profile, thresholds, mode and diagnostics");
  Serial.println("  TEST_ON           Lock autotuning and enter imbalance-test mode");
  Serial.println("  TEST_OFF          Leave test mode and return to monitoring");
  Serial.println("  FORCE_ALARM       Demonstration alarm (never used for learning)");
  Serial.println("  CLEAR_ALARM       Clear demonstration alarm");
  Serial.println("  LED_TEST          Test red, green and blue outputs");
  Serial.println("==========================================================================\n");
}

void performLedTest() {
  setRGB(true, false, false); delay(300);
  setRGB(false, true, false); delay(300);
  setRGB(false, false, true); delay(300);
  applyIndicator();
  Serial.println("[COMMAND] ack=LED_TEST ok=1");
}

void setPendingName(const String& value) {
  String name = value;
  name.trim();
  if (name.length() == 0 || name.length() > 15) {
    Serial.println("[COMMAND] ack=NAME ok=0 reason=LENGTH_MUST_BE_1_TO_15");
    return;
  }
  name.toCharArray(g_pending_profile_id, sizeof(g_pending_profile_id));
  strlcpy(g_candidate.profile_id, g_pending_profile_id, sizeof(g_candidate.profile_id));
  Serial.printf("[COMMAND] ack=NAME ok=1 value=%s revalidation_required=1\n", g_pending_profile_id);
}

void setPendingMass(const String& value) {
  float mass = value.toFloat();
  if (!isfinite(mass) || mass <= 0.0f || mass > 100000.0f) {
    Serial.println("[COMMAND] ack=MASS ok=0 reason=INVALID_GRAMS");
    return;
  }
  g_pending_motor_mass_g = mass;
  g_candidate.motor_mass_g = mass;
  Serial.printf("[COMMAND] ack=MASS ok=1 value_g=%.2f revalidation_required=1\n", mass);
}

void setCustomThresholds(float warnVal, float alarmVal) {
  if (warnVal <= 0.05f || alarmVal <= warnVal) {
    Serial.println("[COMMAND] ack=SET_THRESHOLDS ok=0 reason=INVALID_THRESHOLDS");
    return;
  }
  g_profile.approved = 1;
  g_profile.warning_enter = warnVal;
  float base = g_profile.baseline_rms > 0.01f ? g_profile.baseline_rms : 0.206f;
  g_profile.warning_exit = base + WARN_EXIT_FRACTION * (g_profile.warning_enter - base);
  g_profile.alarm_enter = alarmVal;
  g_profile.alarm_exit = g_profile.warning_enter + ALARM_EXIT_FRACTION * (g_profile.alarm_enter - g_profile.warning_enter);
  saveProfile();
  resetConditionCounters();
  commitState(ConditionState::NORMAL);
  Serial.printf("[COMMAND] ack=SET_THRESHOLDS ok=1 warn_enter=%.5f warn_exit=%.5f alarm_enter=%.5f alarm_exit=%.5f\n",
                g_profile.warning_enter, g_profile.warning_exit, g_profile.alarm_enter, g_profile.alarm_exit);
}

void startStreamingCalibration(int8_t mode) {
  g_stream_cal_mode = mode;
  g_stream_cal_start_ms = millis();
  g_stream_cal_count = 0;
  g_stream_cal_sum = 0.0;
  applyIndicator();
  if (mode == STREAM_CAL_TARE) {
    Serial.println("[CAL_STREAM] ack=TARE_START ok=1 status=RECORDING_TARE min_duration_s=3");
  } else {
    Serial.printf("[CAL_STREAM] ack=CAL_START ok=1 stage=%d status=RECORDING min_duration_s=3\n", mode);
  }
}

void stopStreamingCalibration() {
  if (g_stream_cal_mode == STREAM_CAL_IDLE) {
    Serial.println("[CAL_STREAM] ack=STOP ok=0 reason=NO_ACTIVE_RECORDING");
    return;
  }

  uint32_t elapsed_ms = millis() - g_stream_cal_start_ms;
  if (elapsed_ms < 3000) {
    Serial.printf("[CAL_STREAM] ack=STOP ok=0 reason=MIN_3_SECONDS_REQUIRED elapsed_ms=%lu remaining_ms=%lu count=%u\n",
                  elapsed_ms, 3000 - elapsed_ms, g_stream_cal_count);
    return;
  }

  if (g_stream_cal_count == 0) {
    Serial.println("[CAL_STREAM] ack=STOP ok=0 reason=NO_VALID_SAMPLES_CAPTURED");
    g_stream_cal_mode = STREAM_CAL_IDLE;
    applyIndicator();
    return;
  }

  float avg = static_cast<float>(g_stream_cal_sum / g_stream_cal_count);

  if (g_stream_cal_mode == STREAM_CAL_TARE) {
    g_tare_floor_g = avg;
    g_stream_cal_mode = STREAM_CAL_IDLE;
    saveProfile();
    applyIndicator();
    Serial.printf("[CAL_STREAM] ack=TARE_STOP ok=1 tare_floor=%.5f elapsed_s=%.1f samples=%u\n",
                  g_tare_floor_g, elapsed_ms / 1000.0f, g_stream_cal_count);
  } else {
    int stage = g_stream_cal_mode;
    g_calibrated_stage_vrms[stage] = avg;
    g_stage_calibrated[stage] = true;
    g_stream_cal_mode = STREAM_CAL_IDLE;
    saveProfile();
    applyIndicator();
    Serial.printf("[CAL_STREAM] ack=CAL_STOP ok=1 stage=%d avg_vrms=%.5f elapsed_s=%.1f samples=%u\n",
                  stage, avg, elapsed_ms / 1000.0f, g_stream_cal_count);
  }
}

void commitCalibratedThresholds() {
  float v0 = g_stage_calibrated[0] ? g_calibrated_stage_vrms[0] : 0.206f;
  float v1 = g_stage_calibrated[1] ? g_calibrated_stage_vrms[1] : (v0 + 0.50f);
  float v2 = g_stage_calibrated[2] ? g_calibrated_stage_vrms[2] : (v1 + 0.35f);

  if (v1 <= v0) v1 = v0 + 0.20f;
  if (v2 <= v1) v2 = v1 + 0.25f;

  float warn_enter = v0 + 0.50f * (v1 - v0);
  float warn_exit  = v0 + 0.25f * (v1 - v0);
  float alarm_enter = v1 + 0.50f * (v2 - v1);
  float alarm_exit  = v1 + 0.25f * (v2 - v1);

  g_profile.baseline_rms = v0;
  g_profile.warning_enter = warn_enter;
  g_profile.warning_exit = warn_exit;
  g_profile.alarm_enter = alarm_enter;
  g_profile.alarm_exit = alarm_exit;
  g_profile.approved = 1;
  saveProfile();
  resetConditionCounters();
  commitState(ConditionState::NORMAL);

  Serial.printf("[CAL_STREAM] ack=CAL_COMMIT ok=1 v0=%.5f v1=%.5f v2=%.5f warn_enter=%.5f warn_exit=%.5f alarm_enter=%.5f alarm_exit=%.5f tare=%.5f\n",
                v0, v1, v2, warn_enter, warn_exit, alarm_enter, alarm_exit, g_tare_floor_g);
}

void resetCalibrationDefaults() {
  g_tare_floor_g = 0.0f;
  g_calibrated_stage_vrms[0] = 0.20600f;
  g_calibrated_stage_vrms[1] = 0.84000f;
  g_calibrated_stage_vrms[2] = 1.07900f;
  g_stage_calibrated[0] = false;
  g_stage_calibrated[1] = false;
  g_stage_calibrated[2] = false;

  g_profile.baseline_rms = 0.20600f;
  g_profile.warning_enter = 0.80000f;
  g_profile.warning_exit = 0.75000f;
  g_profile.alarm_enter = 0.95000f;
  g_profile.alarm_exit = 0.88000f;
  g_profile.approved = 1;
  saveProfile();
  resetConditionCounters();
  commitState(ConditionState::NORMAL);

  Serial.println("[CAL_STREAM] ack=CAL_RESET ok=1 baseline=0.20600 warn=0.80000 alarm=0.95000 tare=0.00000");
}

void setCustomAlarm(float alarmVal) {
  if (!g_profile.approved) {
    Serial.println("[COMMAND] ack=SET_ALARM ok=0 reason=NO_APPROVED_PROFILE");
    return;
  }
  if (alarmVal <= g_profile.warning_enter) {
    Serial.println("[COMMAND] ack=SET_ALARM ok=0 reason=ALARM_MUST_BE_GREATER_THAN_WARN");
    return;
  }
  g_profile.alarm_enter = alarmVal;
  g_profile.alarm_exit = g_profile.warning_enter + ALARM_EXIT_FRACTION * (g_profile.alarm_enter - g_profile.warning_enter);
  saveProfile();
  resetConditionCounters();
  commitState(ConditionState::NORMAL);
  Serial.printf("[COMMAND] ack=SET_ALARM ok=1 alarm_enter=%.5f alarm_exit=%.5f\n",
                g_profile.alarm_enter, g_profile.alarm_exit);
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) return;

  String upper = command;
  upper.toUpperCase();

  // Backward-compatible one-character commands from the original firmware.
  if (upper == "0" || upper == "B") recordStage(0);
  else if (upper == "1") recordStage(1);
  else if (upper == "2") recordStage(2);
  else if (upper == "3") recordStage(3);
  else if (upper == "S" || upper == "T") printSummaryTable();
  else if (upper == "C") {
    for (int i = 0; i < 4; ++i) g_benchmarks[i].recorded = false;
    Serial.println("[RESET] Benchmark ledger cleared; approved autotune profile unchanged.");
  }
  else if (upper == "?" || upper == "H") printHelpMenu();
  else if (upper.startsWith("NAME=")) setPendingName(command.substring(5));
  else if (upper.startsWith("MASS=")) setPendingMass(command.substring(5));
  else if (upper == "CAL" || upper == "CAL_AUTO" || upper == "CALIBRATE") {
    g_vibeEngine.startBaselineCalibration();
    Serial.println("[CAL] Started 8.0s automated baseline calibration (Median + MAD)...");
  }
  else if (upper == "CAL_OFF") collectStationaryReference();
  else if (upper == "CAL_RUN") collectHealthyRun();
  else if (upper == "PROFILE_APPROVE") approveCandidate();
  else if (upper == "CAL_CANCEL") cancelCalibration();
  else if (upper == "PROFILE_CLEAR") eraseSavedProfile();
  else if (upper == "STATUS") printStatus();
  else if (upper == "TARE_START" || upper == "TARE") startStreamingCalibration(STREAM_CAL_TARE);
  else if (upper == "TARE_STOP") stopStreamingCalibration();
  else if (upper.startsWith("CAL_START")) {
    int stage = 0;
    String arg = command.substring(9);
    arg.trim();
    if (arg.length() > 0) {
      if (arg.startsWith("=") || arg.startsWith("_") || arg.startsWith(" ")) {
        arg = arg.substring(1);
        arg.trim();
      }
      stage = arg.toInt();
    }
    if (stage < 0 || stage > 2) stage = 0;
    startStreamingCalibration(static_cast<int8_t>(stage));
  }
  else if (upper == "CAL_STOP") stopStreamingCalibration();
  else if (upper == "CAL_COMMIT" || upper == "CAL_SAVE") commitCalibratedThresholds();
  else if (upper == "CAL_RESET" || upper == "CAL_DEFAULT") resetCalibrationDefaults();
  else if (upper.startsWith("SET_ALARM=")) {
    float a = command.substring(10).toFloat();
    setCustomAlarm(a);
  }
  else if (upper.startsWith("SET_THRESHOLDS=") || upper.startsWith("SET_THRESHOLDS ")) {
    String args = command.substring(15);
    args.trim();
    int sep = args.indexOf(',');
    if (sep < 0) sep = args.indexOf(' ');
    if (sep > 0) {
      float w = args.substring(0, sep).toFloat();
      float a = args.substring(sep + 1).toFloat();
      setCustomThresholds(w, a);
    } else {
      Serial.println("[COMMAND] ack=SET_THRESHOLDS ok=0 reason=EXPECTED_TWO_VALUES");
    }
  }
  else if (upper == "TEST_ON") {
    if (!g_profile.approved) {
      Serial.println("[COMMAND] ack=TEST_ON ok=0 reason=NO_APPROVED_PROFILE");
    } else {
      g_mode = OperatingMode::IMBALANCE_TEST;
      resetConditionCounters();
      commitState(ConditionState::NORMAL);
      Serial.println("[COMMAND] ack=TEST_ON ok=1 autotune_locked=1");
    }
  }
  else if (upper == "TEST_OFF") {
    if (!g_profile.approved) {
      Serial.println("[COMMAND] ack=TEST_OFF ok=0 reason=NO_APPROVED_PROFILE");
    } else {
      g_mode = OperatingMode::MONITORING;
      resetConditionCounters();
      commitState(ConditionState::NORMAL);
      Serial.println("[COMMAND] ack=TEST_OFF ok=1 autotune_locked=0");
    }
  }
  else if (upper == "FORCE_ALARM" || upper == "A") {
    g_forced_alarm = true;
    commitState(ConditionState::ALARM);
    Serial.println("[COMMAND] ack=FORCE_ALARM ok=1 forced=1");
  }
  else if (upper == "CLEAR_ALARM" || upper == "N") {
    g_forced_alarm = false;
    resetConditionCounters();
    commitState(g_profile.approved ? ConditionState::NORMAL : ConditionState::UNKNOWN);
    Serial.println("[COMMAND] ack=CLEAR_ALARM ok=1 forced=0");
  }
  else if (upper == "LED_TEST" || upper == "L") performLedTest();
  else {
    Serial.printf("[COMMAND] ack=UNKNOWN ok=0 input=%s\n", command.c_str());
  }
}

void processSerialCommands() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (g_command_buffer.length() > 0) {
        handleCommand(g_command_buffer);
        g_command_buffer = "";
      }
    } else if (g_command_buffer.length() < COMMAND_BUFFER_LIMIT) {
      g_command_buffer += c;
    } else {
      g_command_buffer = "";
      Serial.println("[COMMAND] ack=UNKNOWN ok=0 reason=COMMAND_TOO_LONG");
    }
  }
}

// ============================================================================
// SETUP / LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println("\n==========================================================================");
  Serial.println("VIBEGUARD: AUTOTUNING + SYNCHRONIZED MONITORING FIRMWARE v3");
  Serial.println("Base: VibeGuard_Benchmark_Logger-2.ino");
  Serial.println("==========================================================================");

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  setRGB(false, false, true);

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  delay(50);

  bool profileLoaded = loadProfile();
  resetCandidate(true);

  uint8_t id = readReg(ADXL345_REG_DEVID);
  Serial.printf("[SENSOR] Probing ADXL345 DEVID: 0x%02X ... ", id);
  if (id == ADXL345_DEVID && initializeSensor()) {
    g_sensor_ready = true;
    Serial.println("0xE5 confirmed; 800 Hz, full resolution, +/-16 g.");
    if (profileLoaded) {
      g_mode = OperatingMode::MONITORING;
      g_committed_state = ConditionState::NORMAL;
      Serial.printf("[PROFILE] loaded=1 id=%s baseline=%.5f warning=%.5f alarm=%.5f\n",
                    g_profile.profile_id, g_profile.baseline_rms,
                    g_profile.warning_enter, g_profile.alarm_enter);
    } else {
      g_mode = OperatingMode::NO_PROFILE;
      g_committed_state = ConditionState::UNKNOWN;
      Serial.println("[PROFILE] loaded=0 action=RUN_AUTOTUNE");
    }

    if (g_vibeEngine.loadFromNVS(g_preferences)) {
      Serial.println("[DSP_ENGINE] Loaded Deep Research baseline calibration from NVS.");
    } else {
      Serial.println("[DSP_ENGINE] Default Deep Research baseline active. Send 'CAL' or press BOOT to autotune.");
    }
  } else {
    g_sensor_ready = false;
    g_mode = OperatingMode::SENSOR_FAULT;
    g_committed_state = ConditionState::SENSOR_FAULT;
    Serial.println("ADXL345 not detected. Check power and SPI wiring.");
  }

  applyIndicator();
  printHelpMenu();
  printStatus();
}

void loop() {
  processSerialCommands();

  // Hardware BOOT button (GPIO 0) triggers automated calibration
  if (digitalRead(PIN_BOOT_BUTTON) == LOW && g_vibeEngine.getCalibrationState() == CAL_IDLE) {
    g_vibeEngine.startBaselineCalibration();
    Serial.println("[CAL] BOOT button pressed: Started 8.0s automated baseline calibration (Median + MAD)...");
    delay(250);
  }

  if (!g_sensor_ready) {
    applyIndicator();
    uint32_t now = millis();
    if (now - g_last_sensor_retry_ms >= SENSOR_RETRY_MS) {
      g_last_sensor_retry_ms = now;
      if (readReg(ADXL345_REG_DEVID) == ADXL345_DEVID && initializeSensor()) {
        g_sensor_ready = true;
        g_invalid_window_count = 0;
        g_mode = g_profile.approved ? OperatingMode::MONITORING : OperatingMode::NO_PROFILE;
        resetConditionCounters();
        commitState(g_profile.approved ? ConditionState::NORMAL : ConditionState::UNKNOWN);
        Serial.println("[RECOVERY] sensor=OK reason=ADXL345_RECONNECTED");
      }
    }
    delay(5);
    return;
  }

  // Blocking calibration functions collect their own windows. Normal loop sampling resumes here.
  WindowMetrics metrics = sampleWindowMetrics();
  g_last_metrics = metrics;

  if (!metrics.valid) {
    g_invalid_window_count = incrementSaturated(g_invalid_window_count);
  } else {
    g_invalid_window_count = 0;
  }

  if (!g_sensor_ready || g_invalid_window_count >= INVALID_WINDOWS_FOR_FAULT) {
    if (g_mode != OperatingMode::SENSOR_FAULT) g_mode_before_fault = g_mode;
    g_mode = OperatingMode::SENSOR_FAULT;
    g_sensor_ready = false;
    g_last_sensor_retry_ms = millis();
    commitState(ConditionState::SENSOR_FAULT);
    Serial.printf("[FAULT] reason=%s invalid_windows=%u\n",
                  g_invalid_window_count >= INVALID_WINDOWS_FOR_FAULT ? "INVALID_WINDOWS" : "SENSOR_DISCONNECTED",
                  g_invalid_window_count);
    return;
  }

  if (metrics.valid) {
    if (g_smoothed_vrms < 0.0f) {
      g_smoothed_vrms = metrics.condition_vrms;
    } else {
      g_smoothed_vrms = 0.08f * metrics.condition_vrms + 0.92f * g_smoothed_vrms;
    }
  }

  ConditionState next = evaluateCondition(metrics, metrics.valid);
  commitState(next);
  applyIndicator();

  uint32_t now = millis();
  if (metrics.valid && (g_state_changed || now - g_last_telemetry_ms >= TELEMETRY_INTERVAL_MS)) {
    g_last_telemetry_ms = now;
    WindowMetrics smoothMetrics = metrics;
    if (g_smoothed_vrms >= 0.0f) smoothMetrics.condition_vrms = g_smoothed_vrms;
    printTelemetry(smoothMetrics);
    g_state_changed = false;
  }

  if (now - g_last_status_ms >= STATUS_INTERVAL_MS) {
    g_last_status_ms = now;
    printStatus();
  }

  delay(5);
}
