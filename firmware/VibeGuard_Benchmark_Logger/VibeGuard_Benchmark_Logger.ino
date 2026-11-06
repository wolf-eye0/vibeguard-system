#include <Arduino.h>
#include <SPI.h>
#include <cmath>

// ============================================================================
// VIBEGUARD: INTERACTIVE EXPERIMENTAL BENCHMARK LOGGER & TELEMETRY SYSTEM
// Hardware: 7Semi ESP32-DEVKIT-E + ADXL345 SPI (Certified CS=21, SCK=18, MISO=19, MOSI=23)
// RGB LED:  Green=GPIO 25, Blue=GPIO 26, Red=GPIO 27
// DSP:      800 Hz ODR, ±16g Full-Res, DC Gravity Removal, 3D Vector RMS
// Standard: ISO 10816-3 Mechanical Vibration Severity Standards
// ============================================================================

#define PIN_CS            21
#define PIN_SCK           18
#define PIN_MISO          19
#define PIN_MOSI          23

#define PIN_LED_GREEN     25
#define PIN_LED_BLUE      26
#define PIN_LED_RED       27
#define PIN_BOOT_BUTTON   0

#define ADXL345_REG_DEVID       0x00
#define ADXL345_REG_BW_RATE     0x2C
#define ADXL345_REG_POWER_CTL   0x2D
#define ADXL345_REG_DATA_FORMAT 0x31
#define ADXL345_REG_DATAX0      0x32

#define ADXL345_DEVID           0xE5
#define SCALE_FACTOR            0.00390625f // 3.9 mg/LSB full resolution (±16g)

#define SPI_SPEED               1000000
#define CHIP_SPI_MODE           SPI_MODE2

// DSP Window & ISO 10816 Thresholds
#define BUFFER_SIZE             128
#define RMS_WARN_LIMIT          0.35f   // ISO 10816 Zone B/C boundary (0.35g RMS)
#define RMS_ALARM_LIMIT         0.70f   // ISO 10816 Zone C/D boundary (0.70g RMS)

// ============================================================================
// EXPERIMENTAL BENCHMARK LEDGER
// ============================================================================
struct BenchmarkStep {
  const char* label;
  const char* mass_desc;
  float v_rms;
  bool recorded;
};

BenchmarkStep g_benchmarks[4] = {
  { "0. Baseline Run ", "Bare Arm (0.0g) ", 0.0f, false },
  { "1. Bolt Only    ", "M3x20 Bolt (~2.5g)", 0.0f, false },
  { "2. Bolt + 1 Nut ", "Bolt + 1 Nut (~3.4g)", 0.0f, false },
  { "3. Bolt + 2 Nuts", "Bolt + 2 Nuts (~4.3g)", 0.0f, false }
};

bool g_sensor_ready = false;
int g_alarm_debounce = 0;

void setRGB(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_RED,   r ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, g ? HIGH : LOW);
  digitalWrite(PIN_LED_BLUE,  b ? HIGH : LOW);
}

uint8_t read_reg(uint8_t reg) {
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, CHIP_SPI_MODE));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(5);
  SPI.transfer(reg | 0x80);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  delayMicroseconds(10);
  return val;
}

void write_reg(uint8_t reg, uint8_t val) {
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, CHIP_SPI_MODE));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(5);
  SPI.transfer(reg & 0x7F);
  SPI.transfer(val);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  delayMicroseconds(10);
}

void read_accel_burst(float &ax, float &ay, float &az) {
  uint8_t raw[6];
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, CHIP_SPI_MODE));
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(5);
  SPI.transfer(ADXL345_REG_DATAX0 | 0x80 | 0x40); // Read (0x80) | Multibyte (0x40)
  for (int i = 0; i < 6; i++) {
    raw[i] = SPI.transfer(0x00);
  }
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();

  int16_t x = (int16_t)(raw[0] | (raw[1] << 8));
  int16_t y = (int16_t)(raw[2] | (raw[3] << 8));
  int16_t z = (int16_t)(raw[4] | (raw[5] << 8));

  ax = (float)x * SCALE_FACTOR;
  ay = (float)y * SCALE_FACTOR;
  az = (float)z * SCALE_FACTOR;
}

// Sample 128 data points at 800 Hz and compute AC Vector RMS
float sample_window_rms() {
  float buf_x[BUFFER_SIZE];
  float buf_y[BUFFER_SIZE];
  float buf_z[BUFFER_SIZE];
  float mean_x = 0, mean_y = 0, mean_z = 0;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    unsigned long t0 = micros();
    read_accel_burst(buf_x[i], buf_y[i], buf_z[i]);
    mean_x += buf_x[i];
    mean_y += buf_y[i];
    mean_z += buf_z[i];
    while (micros() - t0 < 1250); // 1250 us = 800 Hz
  }

  mean_x /= BUFFER_SIZE;
  mean_y /= BUFFER_SIZE;
  mean_z /= BUFFER_SIZE;

  // Sanity check: static gravity vector magnitude
  float static_g = sqrt(mean_x * mean_x + mean_y * mean_y + mean_z * mean_z);
  if (static_g < 0.25f) {
    g_sensor_ready = false;
    return -1.0f; // Sensor disconnected
  }

  // Remove DC static gravity bias to isolate dynamic AC vibration
  float sum_sq = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    float dx = buf_x[i] - mean_x;
    float dy = buf_y[i] - mean_y;
    float dz = buf_z[i] - mean_z;
    sum_sq += (dx * dx + dy * dy + dz * dz);
  }

  return sqrt(sum_sq / (float)BUFFER_SIZE);
}

// Interactive helper: Average over multiple windows (e.g. 24 windows = 3.84 sec)
float collect_averaged_rms(int num_windows = 24) {
  setRGB(false, false, true); // Solid BLUE while logging
  float total = 0;
  int valid = 0;
  for (int w = 0; w < num_windows; w++) {
    float r = sample_window_rms();
    if (r >= 0) {
      total += r;
      valid++;
    }
    Serial.print(".");
    delay(5);
  }
  Serial.println();
  return (valid > 0) ? (total / (float)valid) : 0.0f;
}

void print_help_menu() {
  Serial.println("\n==================================================================");
  Serial.println("         VIBEGUARD: INTERACTIVE BENCHMARK LOGGER MENU            ");
  Serial.println("==================================================================");
  Serial.println(" [0] or [b] -> Record Stage 0: BASELINE (Bare 3.35mm Arm, 0 Nuts)");
  Serial.println(" [1]        -> Record Stage 1: MILD UNBALANCE (M3 Bolt Alone)");
  Serial.println(" [2]        -> Record Stage 2: MODERATE UNBALANCE (Bolt + 1 Nut)");
  Serial.println(" [3]        -> Record Stage 3: SEVERE UNBALANCE (Bolt + 2 Nuts)");
  Serial.println(" [s] or [t] -> Print EXPERIMENTAL BENCHMARK SUMMARY TABLE");
  Serial.println(" [c]        -> Clear / Reset Benchmark Ledger");
  Serial.println(" [?] or [h] -> Show this command menu");
  Serial.println("==================================================================\n");
}

void record_stage(int stage_idx) {
  if (stage_idx < 0 || stage_idx > 3) return;
  Serial.printf("\n>>> RECORDING: %s (%s) <<<\n", g_benchmarks[stage_idx].label, g_benchmarks[stage_idx].mass_desc);
  Serial.println("Sampling 24 continuous windows (approx 4 seconds)... Please keep rig running.");

  float avg_rms = collect_averaged_rms(24);
  g_benchmarks[stage_idx].v_rms = avg_rms;
  g_benchmarks[stage_idx].recorded = true;

  float baseline = g_benchmarks[0].v_rms;
  float delta = (g_benchmarks[0].recorded) ? (avg_rms - baseline) : 0.0f;
  float pct = (g_benchmarks[0].recorded && baseline > 0.001f) ? ((delta / baseline) * 100.0f) : 0.0f;

  const char* iso_zone = "Zone A (Good / Smooth)";
  if (avg_rms >= RMS_ALARM_LIMIT) {
    iso_zone = "Zone D (CRITICAL ALARM / Unacceptable)";
    setRGB(true, false, false); // RED
  } else if (avg_rms >= RMS_WARN_LIMIT) {
    iso_zone = "Zone C (WARNING / Restricted Operation)";
    setRGB(true, true, false);  // YELLOW
  } else {
    setRGB(false, true, false); // GREEN
  }

  Serial.println("------------------------------------------------------------------");
  Serial.printf(" [SUCCESS] Logged %s\n", g_benchmarks[stage_idx].label);
  Serial.printf(" * Measured Vector RMS: %5.3f g\n", avg_rms);
  if (stage_idx > 0 && g_benchmarks[0].recorded) {
    Serial.printf(" * Increase Over Baseline: %+5.3f g (%+.1f%%)\n", delta, pct);
  }
  Serial.printf(" * ISO 10816 Classification: %s\n", iso_zone);
  Serial.println("------------------------------------------------------------------");
  Serial.println("Tip: Type 's' to view the full comparative table.\n");
  delay(1000);
}

void print_summary_table() {
  Serial.println("\n==========================================================================================");
  Serial.println("                  VIBEGUARD EXPERIMENTAL UNBALANCE BENCHMARK REPORT                       ");
  Serial.println("==========================================================================================");
  Serial.println("| Stage                   | Unbalance Mass       | Vector RMS  | Delta VRMS | Increase % | ISO Status       |");
  Serial.println("|-------------------------|----------------------|-------------|------------|------------|------------------|");

  float b0 = g_benchmarks[0].recorded ? g_benchmarks[0].v_rms : 0.0f;

  for (int i = 0; i < 4; i++) {
    if (g_benchmarks[i].recorded) {
      float v = g_benchmarks[i].v_rms;
      float d = (i == 0 || !g_benchmarks[0].recorded) ? 0.0f : (v - b0);
      float p = (i == 0 || !g_benchmarks[0].recorded || b0 <= 0.001f) ? 0.0f : ((d / b0) * 100.0f);
      const char* status = (v >= RMS_ALARM_LIMIT) ? "CRITICAL ALARM" : ((v >= RMS_WARN_LIMIT) ? "WARNING" : "NORMAL (Good)");
      Serial.printf("| %-23s | %-20s | %6.3f g    | %+6.3f g   | %+7.1f%%   | %-16s |\n",
                    g_benchmarks[i].label, g_benchmarks[i].mass_desc, v, d, p, status);
    } else {
      Serial.printf("| %-23s | %-20s | [NOT LOGGED]|    --      |    --      | Pending Test     |\n",
                    g_benchmarks[i].label, g_benchmarks[i].mass_desc);
    }
  }
  Serial.println("==========================================================================================");
  Serial.println("Copy & paste the table above directly into your project report or viva presentation!\n");
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println("\n==================================================================");
  Serial.println("   VIBEGUARD: PHYSICAL SENSOR HEALTH & BENCHMARK SYSTEM           ");
  Serial.println("   Hardware: 7Semi ESP32-DEVKIT-E + ADXL345 (SPI Mode 2 Certified)");
  Serial.println("==================================================================");

  pinMode(PIN_LED_RED,     OUTPUT);
  pinMode(PIN_LED_GREEN,   OUTPUT);
  pinMode(PIN_LED_BLUE,    OUTPUT);
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

  setRGB(false, false, true); // Solid Blue during boot self-check

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  delay(50);

  // Initialize ADXL345
  uint8_t id = read_reg(ADXL345_REG_DEVID);
  Serial.printf("[SENSOR] Probing ADXL345 DEVID: 0x%02X ... ", id);

  if (id == ADXL345_DEVID) {
    Serial.println("GENUINE SILICON (0xE5) CONFIRMED!");
    write_reg(ADXL345_REG_POWER_CTL, 0x00);    // Standby
    delay(10);
    write_reg(ADXL345_REG_BW_RATE, 0x0D);      // 800 Hz ODR
    delay(5);
    write_reg(ADXL345_REG_DATA_FORMAT, 0x0B);  // Full-Res, ±16g
    delay(5);
    write_reg(ADXL345_REG_POWER_CTL, 0x08);    // Measurement Mode
    delay(20);

    g_sensor_ready = true;
    setRGB(false, true, false); // Solid Green!
    print_help_menu();
  } else {
    Serial.println("ADXL345 not detected! Please check jumper wiring.");
  }
}

unsigned long g_last_plot_print = 0;

void loop() {
  // 1. Hot-Plug / Auto-Reconnect Check
  if (!g_sensor_ready) {
    setRGB(false, false, true); // Blue search light
    uint8_t id = read_reg(ADXL345_REG_DEVID);
    if (id == ADXL345_DEVID) {
      write_reg(ADXL345_REG_POWER_CTL, 0x00);
      delay(10);
      write_reg(ADXL345_REG_BW_RATE, 0x0D);
      write_reg(ADXL345_REG_DATA_FORMAT, 0x0B);
      write_reg(ADXL345_REG_POWER_CTL, 0x08);
      delay(20);
      g_sensor_ready = true;
      Serial.println("\n[ONLINE] ADXL345 Reconnected Successfully (0xE5)!");
      setRGB(false, true, false);
    } else {
      delay(200);
      return;
    }
  }

  // 2. Interactive Serial Command Handler
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '0' || c == 'b' || c == 'B') {
      record_stage(0);
    } else if (c == '1') {
      record_stage(1);
    } else if (c == '2') {
      record_stage(2);
    } else if (c == '3') {
      record_stage(3);
    } else if (c == 's' || c == 'S' || c == 't' || c == 'T') {
      print_summary_table();
    } else if (c == 'c' || c == 'C') {
      for (int i = 0; i < 4; i++) g_benchmarks[i].recorded = false;
      Serial.println("\n[RESET] Benchmark Ledger Cleared! Type '0' to re-record baseline.\n");
    } else if (c == '?' || c == 'h' || c == 'H') {
      print_help_menu();
    }
  }

  // 3. Live Continuous Real-Time Sampling Window (128 samples at 800 Hz)
  float live_rms = sample_window_rms();
  if (live_rms < 0) return; // Flatlined or disconnected

  // 4. Persistence Filter
  if (live_rms >= RMS_ALARM_LIMIT) {
    if (g_alarm_debounce < 4) g_alarm_debounce++;
  } else {
    if (g_alarm_debounce > 0) g_alarm_debounce--;
  }

  // 5. Indicator LED Control
  if (g_alarm_debounce >= 2) {
    setRGB(true, false, false); // Solid RED (Alarm)
  } else if (live_rms >= RMS_WARN_LIMIT) {
    setRGB(true, true, false);  // YELLOW / AMBER (Warning)
  } else {
    setRGB(false, true, false); // Solid GREEN (Normal)
  }

  // 6. Serial Plotter & Telemetry Streaming (every ~160 ms)
  float baseline_plot = g_benchmarks[0].recorded ? g_benchmarks[0].v_rms : 0.12f;
  
  // Format formatted specifically for Arduino IDE Serial Plotter:
  Serial.printf(">VRMS:%.3f,Baseline:%.3f,WarnLimit:%.2f,AlarmLimit:%.2f\n",
                live_rms, baseline_plot, RMS_WARN_LIMIT, RMS_ALARM_LIMIT);

  // Human Readable Live Print:
  const char* st = (g_alarm_debounce >= 2) ? "ALARM" : ((live_rms >= RMS_WARN_LIMIT) ? "WARNING" : "NORMAL");
  Serial.printf("[LIVE] VRMS: %5.3f g | ISO Status: [%-7s] | Press 0, 1, 2, 3 to Log Step | '?' for Menu\n",
                live_rms, st);

  delay(20);
}
