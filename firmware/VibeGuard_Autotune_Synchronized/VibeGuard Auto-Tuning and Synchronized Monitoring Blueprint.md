# VibeGuard Auto-Tuning and Synchronized Monitoring Blueprint

## Executive overview

VibeGuard should be revised around one authoritative firmware pipeline: the ESP32 measures dynamic baseplate acceleration, applies an approved motor-and-mount profile, derives one filtered condition state, and uses that same state for the RGB LED and serial telemetry. The Python bridge must transport—not independently reinterpret—that state to the browser. This removes the present failure mode in which the dashboard reports ALARM while the LED remains green or yellow.[^1]

The proposed “weight-aware” feature is a **configuration-specific commissioning system**, not a formula that converts motor weight directly into warning and alarm thresholds. Motor mass is useful profile metadata, but the thresholds must come from measured motor-off noise and repeated healthy-running vibration because the acrylic plate, mount stiffness, damping, sensor position, arm geometry, attached load and rotational speed all affect the measured response. Standard vibration-monitoring guidance likewise treats transducer location, attachment, operating condition and measurement procedure as essential parts of the measurement.[^2][^3]

The controlled nut experiment remains valid as an imbalance demonstration. The normal configuration is learned first and then locked; added eccentric nuts are recorded as test conditions and must never become new training data. The intended claim is: **VibeGuard detects changes in acceleration at the shared acrylic baseplate caused by deliberately introduced rotating imbalance**.[^4]

## System boundary

### Physical measurement

The signal path is:

```text
Motor and eccentric arm force
        ↓
Motor mount and fasteners
        ↓
Acrylic baseplate response
        ↓
Rigidly mounted ADXL345
        ↓
ESP32 dynamic-acceleration metrics
        ↓
Condition state and RGB LED
        ↓
USB serial → bridge.py → WebSocket → dashboard
```

The ADXL345 measures three-axis acceleration at its mounting point; it does not directly weigh the motor, identify a bearing defect, or measure shaft imbalance. It supports selectable ranges up to ±16 g, with full-resolution scaling of approximately 4 mg/LSB, and its measurement bandwidth is half the selected output-data rate. The project should therefore describe its output as **transmitted vibration of the motor–mount–baseplate assembly**.[^5][^6]

Earth-rotation compensation is unnecessary. The relevant corrections are static bias, gravity/orientation, mounting repeatability and dynamic noise. Analog Devices recommends correcting initial bias by measuring output with no stimulus and subtracting the error; the ADXL345 also provides offset registers, although finer calibration can be performed in the processor.[^7][^8]

### Authoritative responsibilities

| Layer | Authoritative responsibility | Must not do |
|---|---|---|
| ESP32 firmware | Sampling, dynamic metrics, profile management, thresholds, hysteresis, persistence, final state, RGB output | Continuously relearn during monitoring |
| `bridge.py` | Own serial port, parse messages, validate schema, relay telemetry, forward approved commands | Recalculate NORMAL/WARNING/ALARM independently |
| Website | Display firmware state, trends, profile status, controls, events and freshness | Infer a different alarm state from raw RMS |
| Operator | Confirm healthy commissioning condition, enter profile metadata, approve candidates, secure test masses | Calibrate while imbalance is installed |

## Core operating modes

The firmware must expose a top-level operating mode separate from the vibration condition.

| Operating mode | Purpose | RGB behavior | Dashboard behavior |
|---|---|---|---|
| `NO_PROFILE` | No approved calibration is available | Blue slow pulse | “Commissioning required”; no NORMAL claim |
| `MOTOR_OFF_REFERENCE` | Measure stationary bias and noise | Blue pulse | Progress and stationary-quality checks |
| `WAIT_HEALTHY_RUN` | Wait for operator-confirmed normal motor operation | Blue steady | Instructions; monitoring classification suppressed |
| `LEARNING_HEALTHY` | Gather repeated normal-run windows | Blue/white pulse | Run quality, elapsed time and consistency |
| `AWAIT_APPROVAL` | Candidate thresholds computed but not active | Purple/blue steady | Review baseline, spread and thresholds |
| `MONITORING` | Approved profile is active | Green/yellow/red from final state | Live condition and trends |
| `IMBALANCE_TEST` | Approved thresholds frozen for nut experiment | Green/yellow/red from final state | Test metadata and “autotune locked” |
| `SENSOR_FAULT` | Sensor or acquisition is unreliable | Distinct fault pattern | Fault overrides condition display |

The vibration condition enum should be:

```cpp
enum class ConditionState : uint8_t {
  UNKNOWN,
  NORMAL,
  WARNING,
  ALARM,
  SENSOR_FAULT
};
```

Only one function may commit `ConditionState`. The LED routine, telemetry serializer and event logger must consume the committed state rather than duplicating threshold logic.[^1]

## Profile model

### Profile identity

Each approved profile represents the entire mechanical and signal-processing configuration—not merely a motor. Store:

```text
schema_version
profile_id
profile_name
motor_mass_g
normal_arm_description
load_description
sensor_position_id
sensor_orientation_id
mounting_revision
motor_mount_revision
controller_setting
nominal_rpm (optional; null if unavailable)
sample_rate_hz
window_samples
window_hop_samples
range_g
stationary_axis_mean_x/y/z
stationary_noise_rms
healthy_baseline_median
healthy_spread_mad
warning_enter
warning_exit
alarm_enter
alarm_exit
warning_persistence_windows
alarm_persistence_windows
recovery_persistence_windows
commissioned_at
approved
profile_crc
```

Motor mass must be manually entered or obtained from a separate weighing device. The ADXL345 cannot infer static motor weight reliably from a completed acrylic assembly. A changed motor, normal arm arrangement, sensor location, sensor mounting, baseplate support or processing configuration requires profile revalidation. Added eccentric nuts used for a test do **not** trigger recommissioning; they are disturbance metadata.[^4]

### Persistence

Use Arduino-ESP32 `Preferences` to save only approved profiles and infrequent configuration changes. Preferences stores key-value data in ESP32 non-volatile storage and retains it across restart or loss of power. Do not write each telemetry sample or each state transition to NVS.[^9][^10]

Use a short namespace such as `vgprof`; ESP32 Preferences currently limits namespace and key names to 15 characters. Save a schema version and CRC/check value, retain the previous approved profile until the replacement is validated, and fall back to `NO_PROFILE` if the saved record is incomplete or invalid.[^10][^9]

## Signal processing

### Acquisition settings

Retain 800 Hz as the initial acquisition rate while validating the current installation. At an 800 Hz output-data rate, the ADXL345’s nominal measurement bandwidth is 400 Hz because bandwidth is half ODR. Ensure the SPI read and processing loop can actually consume every data-ready sample; selecting an output-data rate too high for the communication path can lead to discarded samples.[^5][^7]

Use a 64-sample window as the first response-time trial and a 32-sample hop:

- Window duration: 80 ms at 800 Hz.
- Decision update: every 40 ms with 50% overlap.
- Warning persistence starting point: 3 updates, approximately 120 ms.
- Alarm persistence starting point: 2 updates, approximately 80 ms.
- Recovery persistence starting point: 8 updates, approximately 320 ms.

These are **engineering starting points**, not final safety or diagnostic limits. The current 128-sample, non-overlapping window lasts approximately 160 ms, and two alarm windows can already create roughly 320 ms of acquisition/decision delay before serial and browser latency. Overlap improves update cadence without reducing the analysis window, but CPU and timing must be verified.[^1]

### Dynamic acceleration

For each axis, subtract the mean of the current window:

\[
d_i[n] = a_i[n] - \overline{a_i}
\]

Then calculate per-axis RMS:

\[
R_i = \sqrt{\frac{1}{N}\sum_{n=1}^{N}d_i[n]^2}
\]

Calculate vector dynamic RMS:

\[
R_v = \sqrt{R_x^2 + R_y^2 + R_z^2}
\]

Calculate peak vector and crest factor from the same demeaned window:

\[
P_v = \max_n \sqrt{d_x[n]^2+d_y[n]^2+d_z[n]^2}
\]

\[
C = \frac{P_v}{R_v}
\]

If `R_v` is below a small validated denominator guard, transmit crest factor as `null` rather than an extreme or fabricated value. Demeaning each window suppresses gravity and slow offset changes; the exact window and filter settings must therefore remain part of the profile.

### Sample-quality gates

A window is valid only if all of the following hold:

- The expected number of data-ready samples arrived.
- No axis value exceeded the selected range’s clipping margin.
- No sample is NaN, infinite or outside the physical range.
- Timing jitter and missed-sample counts remain under validated limits.
- Sensor identity and configuration registers are valid.
- The acquisition mode is not changing.

Invalid windows must not update the baseline, persistence counters or charts. They increment diagnostics and can escalate to `SENSOR_FAULT` after a configured number of consecutive failures.

## Commissioning algorithm

### Phase A: stationary reference

1. Require the motor to be powered off and the rotating arm fully stopped.
2. Ask the operator to confirm that no eccentric test nuts are installed.
3. Collect a five-second candidate reference.
4. Calculate axis means, dynamic RMS distribution, missing samples and clipping.
5. Reject the run if motion exceeds a provisional stationarity limit derived from the sample distribution or if acquisition quality fails.
6. Store the accepted axis means and stationary noise as candidate data, not yet as an approved profile.

The stationary phase characterizes bias, gravity orientation and baseplate noise. ADXL345 offset calibration can use device offset registers, but their resolution is coarser than processor-side correction and their values do not persist through sensor power removal unless the ESP32 rewrites them at initialization.[^11][^7]

### Phase B: healthy baseline

1. Require an operator-confirmed, mechanically normal configuration.
2. Start the motor at the intended controller setting.
3. Wait a configurable settling interval, initially five seconds.
4. Collect 15–30 seconds of valid overlapping windows.
5. Stop and repeat for at least three independent runs.
6. Compare run medians and spreads; reject the commissioning set if repeatability is poor.
7. Compute candidate baseline and robust spread from all accepted windows.
8. Present candidate thresholds for approval; do not silently activate them.

A steady signal is not proof that a motor is healthy. “Healthy” is a commissioning assumption confirmed by the operator/supervisor.

### Robust statistics

Let:

- `B` be the median of accepted healthy-running `R_v` windows.
- `MAD` be `median(abs(R_v - B))`.
- `S` be `1.4826 × MAD`, a robust spread estimate when the distribution is approximately normal.
- `F` be the median stationary dynamic RMS.

Candidate enter thresholds:

\[
T_{W,enter}=B+\max(k_W S,\Delta_W,c_WF)
\]

\[
T_{A,enter}=\max\left(B+\max(k_A S,\Delta_A,c_AF),T_{W,enter}+\Delta_{gap}\right)
\]

Candidate exit thresholds:

\[
T_{W,exit}=B+h_W(T_{W,enter}-B)
\]

\[
T_{A,exit}=T_{W,enter}+h_A(T_{A,enter}-T_{W,enter})
\]

Initial tuning constraints—not final values—should enforce `k_A > k_W`, `0 < h_W < 1`, `0 < h_A < 1`, `T_W,exit < T_W,enter`, and `T_A,exit < T_A,enter`. Parameters must be selected from the observed separation between normal and controlled imbalance tests. Do not reuse generic fixed values such as 0.35 g and 0.70 g or fixed multipliers such as 1.8 and 2.5 without experimental validation.[^1]

### Mass-aware rule

Motor mass is used in three ways:

- Profile identity and traceability.
- A validation prompt when the declared motor changes materially.
- Experimental analysis when comparing different motor assemblies.

Motor mass is **not** used as a direct multiplier for thresholds. The measured response already captures the combined effects of mass, stiffness, damping, speed and mounting. Automatically increasing thresholds for a heavier motor could hide a genuine vibration increase.

### Commissioning pseudocode

```cpp
bool runCommissioning(const ProfileMetadata& meta) {
  CandidateProfile c{};
  c.meta = meta;

  if (!confirmMotorOff() || testMassInstalled()) return false;
  auto offRun = collectWindows(5000, STATIONARY);
  if (!qualityPass(offRun) || !stationaryPass(offRun)) return false;
  c.offMean = axisMean(offRun);
  c.noiseFloor = medianRms(offRun);

  for (int run = 0; run < 3; ++run) {
    waitForHealthyRunConfirmation(run);
    delayNonBlocking(SETTLE_MS);
    auto healthy = collectWindows(LEARN_MS, RUNNING);
    if (!qualityPass(healthy)) return false;
    c.healthyRuns.push_back(summarize(healthy));
  }

  if (!repeatabilityPass(c.healthyRuns)) return false;
  deriveRobustBaseline(c);
  deriveCandidateThresholds(c);
  publishCandidate(c);
  return true; // approval is a separate command
}
```

## Unified condition engine

### Hysteresis and persistence

Use separate enter and exit thresholds to prevent flicker. Use symmetric evidence counters for warning and alarm, but allow a faster escalation than recovery. Dashboard and LED synchronization requires the **committed filtered state**, not a raw comparison, to be published.[^1]

```cpp
ConditionState updateCondition(float rms, bool windowValid) {
  if (!sensorHealthy() || tooManyInvalidWindows()) return SENSOR_FAULT;
  if (!activeProfile.approved || operatingMode != MONITORING &&
      operatingMode != IMBALANCE_TEST) return UNKNOWN;
  if (!windowValid) return committedState;

  bool alarmEvidence = rms >= profile.alarmEnter;
  bool warningEvidence = rms >= profile.warningEnter;
  bool alarmReleased = rms < profile.alarmExit;
  bool warningReleased = rms < profile.warningExit;

  alarmCount = alarmEvidence ? saturatingInc(alarmCount) : decay(alarmCount);
  warnCount = warningEvidence ? saturatingInc(warnCount) : decay(warnCount);

  switch (committedState) {
    case NORMAL:
      if (alarmCount >= profile.alarmPersist) return ALARM;
      if (warnCount >= profile.warnPersist) return WARNING;
      return NORMAL;

    case WARNING:
      if (alarmCount >= profile.alarmPersist) return ALARM;
      if (warningReleased && ++recoveryCount >= profile.recoveryPersist)
        return NORMAL;
      resetRecoveryIfNeeded(warningReleased);
      return WARNING;

    case ALARM:
      if (alarmReleased && ++recoveryCount >= profile.recoveryPersist)
        return (rms >= profile.warningExit) ? WARNING : NORMAL;
      resetRecoveryIfNeeded(alarmReleased);
      return ALARM;

    default:
      return NORMAL;
  }
}
```

### Single commit path

```cpp
void commitState(ConditionState next) {
  if (next != committedState) {
    logStateTransition(committedState, next);
    committedState = next;
  }
  applyRgb(committedState, operatingMode);
  telemetry.condition = committedState;
}
```

Priority must be:

```text
SENSOR_FAULT
    > commissioning/unknown state
    > forced demonstration alarm
    > measured ALARM
    > measured WARNING
    > NORMAL
```

A forced alarm must be explicitly transmitted as `forced_alarm: true`, and it must never be included in tuning data.

## Imbalance-test workflow

### Configuration rules

The approved baseline represents the intended normal arm configuration. Added nuts are deliberately eccentric test masses and must be placed away from the shaft axis to create a changed mass distribution; balanced equal masses can reduce rather than increase unbalance.[^4]

Before each test, record:

```text
test_id
profile_id
nut_count
combined_added_mass_g
radius_from_axis_mm
angular_position_description
controller_setting
measured_rpm (optional)
operator
start/end timestamps
```

Do not claim that nut count alone predicts severity. The excitation depends on eccentric mass, radius and rotational speed, while the measured baseplate response also depends on the structure.[^4]

### Test sequence

1. Commission and approve the normal configuration.
2. Run a normal verification trial and confirm readings remain inside the approved healthy distribution.
3. Stop and isolate motor power.
4. Install a mechanically secured, supervisor-approved eccentric mass.
5. Close the protective enclosure.
6. Enter test metadata and switch to `IMBALANCE_TEST`; freeze thresholds.
7. Run at the same controller setting or measured speed.
8. Record state transitions, latency and vibration distributions.
9. Stop and isolate power before changing mass.
10. Restore the normal configuration and verify recovery toward baseline.

Rotating parts and possible flying objects require guarding; OSHA’s general machine-guarding guidance specifically identifies rotating parts and flying objects as hazards and calls for guards that prevent access to the danger zone. The blueprint therefore does not prescribe nut count, mass, radius or maximum RPM; those limits require the arm design, fastener retention, motor rating, enclosure and supervisor’s mechanical safety assessment.[^12][^13]

## Firmware telemetry protocol

Use newline-delimited JSON if program memory and existing parser constraints permit. Otherwise retain tagged lines but normalize them in `bridge.py`. JSON lines reduce ambiguous parsing.

### Telemetry message

```json
{"type":"telemetry","schema":3,"seq":1842,"device_ms":983421,"mode":"IMBALANCE_TEST","condition":"ALARM","forced_alarm":false,"profile":{"id":"M01-A","approved":true,"motor_mass_g":320.0,"autotune_locked":true},"accel":{"unit":"g","rms":{"x":0.081,"y":0.126,"z":0.054,"vector":0.159},"peak_vector":0.402,"crest_factor":2.53},"thresholds":{"warning_enter":0.110,"warning_exit":0.092,"alarm_enter":0.150,"alarm_exit":0.130},"quality":{"window_valid":true,"missed_samples":0,"clipped":false}}
```

### Status message

```json
{"type":"status","schema":3,"mode":"MONITORING","sensor_ok":true,"spi_mode":3,"odr_hz":800,"window_samples":64,"hop_samples":32,"profile":{"id":"M01-A","approved":true,"baseline_rms_g":0.061,"spread_g":0.008},"diagnostics":{"invalid_windows":0,"data_ready_timeouts":0}}
```

### Commissioning messages

```json
{"type":"calibration","stage":"MOTOR_OFF_REFERENCE","progress":0.64,"quality":"PASS_SO_FAR"}
{"type":"calibration_candidate","profile_id":"M01-A","baseline_rms_g":0.061,"spread_g":0.008,"warning_enter_g":0.110,"alarm_enter_g":0.150,"approval_required":true}
{"type":"command_ack","request_id":"c42","command":"APPROVE_PROFILE","ok":true}
```

Values shown are schema examples only, not recommended thresholds.

### Publication timing

- Sample acquisition follows DATA_READY at the configured ODR.
- Condition engine runs at each window hop.
- Compact telemetry publishes every 100–200 ms and immediately on state transition.
- Status publishes every 1–2 seconds and after configuration changes.
- Verbose diagnostics publish on request or at a slower interval.

The current plan should remove duplicate plotter and verbose output from every decision iteration because serial formatting and transmission can add latency.[^1]

## Python bridge design

### Port layout

Use distinct ports with the existing standalone `websockets` library:

```text
Website HTTP server: http://127.0.0.1:8000
WebSocket bridge:    ws://127.0.0.1:8765
ESP32 serial:        COMx at firmware baud rate
```

The Python `websockets` documentation recommends a separate HTTP server or an HTTP framework when full HTTP and WebSocket services are needed, rather than trying to make two independent listeners bind the same port. Only one process should bind TCP port 8765, and only `bridge.py` should own the ESP32 COM port.[^14][^15]

### Bridge pipeline

```text
serial reader thread/task
    ↓
line framing and UTF-8 cleanup
    ↓
JSON/tagged-line parser
    ↓
schema and numeric validation
    ↓
latest-state cache + bounded history
    ↓
async WebSocket broadcast
```

`bridge.py` responsibilities:

- Configure the correct COM port, baud, 8 data bits, no parity, one stop bit and read timeout.
- Close the serial handle on shutdown and reconnect with bounded backoff.
- Reject malformed packets without terminating the bridge.
- Add `received_at` timestamps while preserving `device_ms` and sequence.
- Broadcast firmware condition unchanged.
- Send the latest status and telemetry immediately to a newly connected browser.
- Validate browser commands against an allowlist before writing to serial.
- Track raw lines, parse failures, sequence gaps and last-packet age.

PySerial opens the port when `serial.Serial` is constructed with a port and supports explicit timeout and framing configuration. PlatformIO Serial Monitor must remain closed while `bridge.py` owns the port.[^16][^17]

### Browser command mapping

```text
request_status          → S
start_stationary_ref    → new explicit firmware command
start_healthy_learning  → new explicit firmware command
cancel_learning         → new explicit firmware command
approve_profile         → new explicit firmware command
enter_test_mode         → new explicit firmware command
exit_test_mode          → new explicit firmware command
force_alarm_demo        → A
clear_forced_alarm      → N
led_test                → L
```

Do not overload the old calibration command with multiple meanings. Add versioned commands and acknowledgements containing a request ID.

## Dashboard changes

### Authoritative state display

The dashboard must show `packet.condition` exactly as committed by the ESP32. It may display a separate threshold preview for diagnostics, but must never use that preview to overwrite the visible state. The RGB diagram, headline condition card and event log must all consume the same field.

### Autotune wizard

Create a guarded workflow:

1. **Profile details:** name, motor mass, normal arm arrangement, sensor location, mounting revision and controller setting.
2. **Safety check:** motor stopped, no test nuts, fasteners secure and guard available.
3. **Stationary reference:** progress, noise, missing samples and pass/reject reason.
4. **Healthy runs:** settling timer, three-run progress and consistency.
5. **Candidate review:** baseline, robust spread, warning/alarm enter and exit thresholds, expected persistence delay.
6. **Approval:** explicit operator confirmation and save result.
7. **Verification:** short normal run; enable monitoring only if quality passes.

During tuning, display `CALIBRATING`, not NORMAL. If tuning fails, retain the previous approved profile and show the rejection reason.

### Imbalance-test panel

Display:

```text
Active baseline: M01-A / normal arm
Test condition: eccentric mass installed
Autotune: LOCKED
Added mass: operator-entered
Radius: operator-entered
Speed: measured or “not measured”
```

Provide `Begin test`, `End test` and `Export run` actions. Never provide a button that automatically adopts the current alarm condition as the new baseline.

### Latency diagnostics

Record four timestamps:

- `window_end_device_ms`
- `state_committed_device_ms`
- `bridge_received_at`
- `browser_rendered_at`

Report acquisition/window delay, persistence delay, transport delay and render delay separately. This makes it possible to determine whether a slow response comes from the RMS window, debounce, serial publication, bridge or browser.

## Validation programme

### Bench tests

| Test | Procedure | Pass criterion |
|---|---|---|
| LED/dashboard agreement | Exercise all state transitions | LED state and transmitted condition never disagree after the same commit |
| Stationary rejection | Move/tap plate during motor-off phase | Calibration rejected; old profile preserved |
| Repeatability | Perform three healthy runs | Consistency gate passes only within experimentally selected tolerance |
| Persistence | Inject threshold-adjacent readings | No single-window nuisance state unless designed |
| Hysteresis | Sweep above and below thresholds | No rapid flicker near boundary |
| Sensor fault | Disconnect or invalidate ADXL345 | Fault overrides NORMAL/WARNING/ALARM |
| Missing samples | Simulate data-ready timeouts | Invalid windows excluded; diagnostic counter increments |
| Profile persistence | Approve, power-cycle, reload | Same valid profile and CRC recovered |
| Test lock | Enter imbalance mode and request tuning | Request rejected; thresholds unchanged |
| Reconnect | Unplug/reconnect ESP32 | Bridge recovers without duplicate socket/serial ownership |

### Experimental trials

For each approved normal configuration:

1. Perform at least three normal trials.
2. Perform repeated controlled imbalance trials at each supervisor-approved mass/radius configuration.
3. Keep speed as constant as practical and record measured RPM when available.
4. Randomize or alternate normal and imbalance runs where practical to reduce drift bias.
5. Compare distributions, false warnings, detection rate and response latency.
6. Restore the normal configuration and verify recovery.

Do not tune thresholds merely so “one nut equals WARNING” and “two nuts equal ALARM.” Severity labels must follow repeatable separation in measurements, not a predetermined nut count.[^4]

## Implementation sequence

### Stage 1: correctness

- Add `OperatingMode` and `ConditionState` enums.
- Centralize `updateCondition()` and `commitState()`.
- Drive RGB and telemetry exclusively from `committedState`.
- Add sequence number and state-transition telemetry.
- Verify dashboard/LED agreement before changing thresholds.

### Stage 2: signal timing

- Implement dynamic per-axis demeaning and quality flags.
- Trial 64-sample windows with 32-sample hops at 800 Hz.
- Add separate warning, alarm and recovery persistence.
- Add hysteresis thresholds.
- Rate-limit serial telemetry and publish transitions immediately.

### Stage 3: commissioning

- Add motor-off and healthy-running acquisition states.
- Implement robust baseline/spread calculation.
- Add candidate threshold generation and approval.
- Save approved profiles with Preferences and schema/CRC validation.
- Preserve previous profile on all failure paths.

### Stage 4: bridge and website

- Extend `bridge.py` parser for schema 3.
- Keep HTTP and WebSocket listeners on distinct ports.
- Add command allowlist and acknowledgements.
- Make browser state firmware-authoritative.
- Add autotune wizard, profile display, test lock and latency diagnostics.

### Stage 5: controlled validation

- Establish normal repeatability.
- Conduct guarded eccentric-mass tests.
- Select final statistical margins, hysteresis and persistence from data.
- Freeze the release profile and document its scope.

## Acceptance criteria

The operation is ready only when all conditions below are met:

- The RGB LED, telemetry condition and dashboard headline derive from one committed firmware state.
- No approved profile means no green NORMAL claim.
- Motor mass is stored as metadata, not used as an unsupported threshold multiplier.
- Stationary and healthy-running phases are separate and quality-gated.
- At least three healthy runs are required before candidate approval.
- Added imbalance nuts cannot trigger or influence autotuning.
- Thresholds remain frozen throughout an imbalance test.
- Invalid/clipped/missing-sample windows cannot train the profile.
- A failed commissioning attempt preserves the previous approved profile.
- Hysteresis and persistence apply to both LED and dashboard state.
- Profile data survives power cycling and passes version/CRC validation.
- `bridge.py` exclusively owns the serial port, and no HTTP/socket port collision occurs.
- The dashboard distinguishes live, stale, disconnected, calibrating, test and sensor-fault states.
- The enclosure and secured-mass procedure are approved before rotating imbalance trials.
- Project claims remain limited to detecting changes in transmitted baseplate vibration, not diagnosing a specific motor fault.

## Final engineering position

The best VibeGuard design is not a sensor that “tunes itself to motor weight.” It is a controlled commissioning system that identifies the complete motor–arm–mount–baseplate–sensor configuration, measures its stationary noise and operator-confirmed normal vibration, proposes robust thresholds, requires approval, and then locks those thresholds during deliberate imbalance tests. Combined with a single firmware state machine, this approach addresses both accuracy and synchronization while preserving the experimental purpose of adding eccentric nuts.

---

## References

1. [so-the-reading-isnt-exactly-accurate-so-like-when.pdf](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/144580388/30eb40b9-87a9-4066-bcba-632553ec6945/so-the-reading-isnt-exactly-accurate-so-like-when.pdf?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Checksum-Mode=ENABLED&X-Amz-Credential=ASIA2F3EMEYE4PFS4UJ5%2F20261012%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261012T095547Z&X-Amz-Expires=2591&X-Amz-Security-Token=IQoJb3JpZ2luX2VjELr%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FwEaCXVzLWVhc3QtMSJHMEUCIDGCIO8Aco5suiL7kXqdbVB9ohkWKuaNExYVS1SeTzlmAiEAvvBVQWjPRyI9G%2Fxk2Hyase3Ts6loNTI4MGaRDxi4luYqiQUIgv%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FARABGgw2OTk3NTMzMDk3MDUiDJBpiXez9YQ7%2FWP75CrdBE6mTGge004xoLmi49rd582HAjuIP1jDGJfAol2yB%2FcLdpojxsOBzfJA5dcg13r4Xvrc8vQnPaJ0EANVQEzqmU3ixJug%2BrYM%2BNux2zr3Tz%2BIhCPPgM7cky7aWBHFVMuh3cQdwkvZWFdOBFeEHdDXBdUx%2FC3PiamGyNI6%2FRzcEVINoZ9A7g%2F6CCXsn%2BUrVvNEqwvhrFwStSs3UKVMlVSMMR67u3ZePS0OacPUlsmYIYW%2BWa20sXITYiqk4WoONR%2BMLD6H4rld%2BQzrFBF1OsJY405V6LHUOH4vfpBwKmpgaXXhFiPdFvZQRwem5cLo3IhEzaJtjItTb1bvUNCLsVksShnQYE6DdiL1SzluC4UeXhotDBFTqT9VwXixvBwjTNBCbb538dZpg9WfqVW%2BaFkbT6W5J9X1ubrE4%2FcHE14QLa%2Fx9OTg9e%2FE2i5li4IK19CQUKbaDFHA6uyB7qsvIo9VGxWGjI131TapGtDMg02MqiVz6mBTzXU9fDBQmbgzSENrNL0rwzREQb0cKNB8zJ38cJA8JOY9LWvAOFkxq50e%2FKk2CgqvUgvTi%2BtKloMs7tdHUFaL8lO6R7LR2S%2FeDmFjpiaPbkLsu41aR%2BGl60UdHfqa%2BSWckvlNgavSU0H%2F5SSwklxCw96pr7nAeegKRUKSFAjOUVAPyIdxv8nMSAf7YLmegg7mlrxVFbmhjss2XTVNQmJokBxnij7KgHo2o%2FCgGpEg4xLSn%2FCPD8NnBzHJ8renvi7a%2F0pRlX%2BTAh%2FDNDFvGJ0kNKDlBSQVVpgwfkM7bIMbTshpOkse7vbfagegMO%2FVstYGOpgBQZCjacsARk92gA70nm%2BK14F%2BUeY%2BpZscAN2ZEnBuZc4f4LtV4csJD2f1GOj6Ti2defJeAJ7u9r58iuXsvOkG7cpB%2FAN7NPDrdXjnVQIn0BK7XRWjKw%2Bv1CNXzRqy6%2FvRZGnN0jXrxv%2FnlViNlimGYn3WD%2BO2seKX%2FVsF0fV3O9SbGREQ9DkZA0%2FbyJ4fmpEE40V1tKI3%2BzY%3D&X-Amz-SignedHeaders=host&x-id=GetObject&X-Amz-Signature=d2dc2fa4449feda3deb8c9550ddd5cb1f94108cdb36ec399a2886785c5b0bfae) - page-1 Yesyou need to tune it, but the main issue is not sensor inaccuracy alone. The biggerproblem ...

2. [ISO 13373-1:2002](https://www.iso.org/standard/21831.html) - Condition monitoring and diagnostics of machines — Vibration condition monitoring — Part 1: General ...

3. [ISO 5348:2021 - Mechanical Mounting of Accelerometers Standard ...](https://standards.iteh.ai/catalog/standards/iso/88049155-6536-4e83-aeef-ffc689346258/iso-5348-2021) - ISO 5348:2021 guides mechanical mounting of accelerometers to ensure accurate vibration measurement,...

4. [as-we-are-adding-nuts-to-the-arms-attached-to-moto.pdf](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/144580388/5022ebe4-0a1d-4ba2-b4cf-ce2ad00520be/as-we-are-adding-nuts-to-the-arms-attached-to-moto.pdf?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Checksum-Mode=ENABLED&X-Amz-Credential=ASIA2F3EMEYE4PFS4UJ5%2F20261012%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261012T095547Z&X-Amz-Expires=2591&X-Amz-Security-Token=IQoJb3JpZ2luX2VjELr%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FwEaCXVzLWVhc3QtMSJHMEUCIDGCIO8Aco5suiL7kXqdbVB9ohkWKuaNExYVS1SeTzlmAiEAvvBVQWjPRyI9G%2Fxk2Hyase3Ts6loNTI4MGaRDxi4luYqiQUIgv%2F%2F%2F%2F%2F%2F%2F%2F%2F%2FARABGgw2OTk3NTMzMDk3MDUiDJBpiXez9YQ7%2FWP75CrdBE6mTGge004xoLmi49rd582HAjuIP1jDGJfAol2yB%2FcLdpojxsOBzfJA5dcg13r4Xvrc8vQnPaJ0EANVQEzqmU3ixJug%2BrYM%2BNux2zr3Tz%2BIhCPPgM7cky7aWBHFVMuh3cQdwkvZWFdOBFeEHdDXBdUx%2FC3PiamGyNI6%2FRzcEVINoZ9A7g%2F6CCXsn%2BUrVvNEqwvhrFwStSs3UKVMlVSMMR67u3ZePS0OacPUlsmYIYW%2BWa20sXITYiqk4WoONR%2BMLD6H4rld%2BQzrFBF1OsJY405V6LHUOH4vfpBwKmpgaXXhFiPdFvZQRwem5cLo3IhEzaJtjItTb1bvUNCLsVksShnQYE6DdiL1SzluC4UeXhotDBFTqT9VwXixvBwjTNBCbb538dZpg9WfqVW%2BaFkbT6W5J9X1ubrE4%2FcHE14QLa%2Fx9OTg9e%2FE2i5li4IK19CQUKbaDFHA6uyB7qsvIo9VGxWGjI131TapGtDMg02MqiVz6mBTzXU9fDBQmbgzSENrNL0rwzREQb0cKNB8zJ38cJA8JOY9LWvAOFkxq50e%2FKk2CgqvUgvTi%2BtKloMs7tdHUFaL8lO6R7LR2S%2FeDmFjpiaPbkLsu41aR%2BGl60UdHfqa%2BSWckvlNgavSU0H%2F5SSwklxCw96pr7nAeegKRUKSFAjOUVAPyIdxv8nMSAf7YLmegg7mlrxVFbmhjss2XTVNQmJokBxnij7KgHo2o%2FCgGpEg4xLSn%2FCPD8NnBzHJ8renvi7a%2F0pRlX%2BTAh%2FDNDFvGJ0kNKDlBSQVVpgwfkM7bIMbTshpOkse7vbfagegMO%2FVstYGOpgBQZCjacsARk92gA70nm%2BK14F%2BUeY%2BpZscAN2ZEnBuZc4f4LtV4csJD2f1GOj6Ti2defJeAJ7u9r58iuXsvOkG7cpB%2FAN7NPDrdXjnVQIn0BK7XRWjKw%2Bv1CNXzRqy6%2FvRZGnN0jXrxv%2FnlViNlimGYn3WD%2BO2seKX%2FVsF0fV3O9SbGREQ9DkZA0%2FbyJ4fmpEE40V1tKI3%2BzY%3D&X-Amz-SignedHeaders=host&x-id=GetObject&X-Amz-Signature=d0cabe1254b1dc235f2bb905cd9e72d53d62e41c147e9adf38c58b9a0340d653) - page-1 Yesadding nuts to the rotating arm can support your project as a controlled imbalancedemonstr...

5. [[PDF] Data Sheet - ADXL345 - Analog Devices](https://www.analog.com/media/en/technical-documentation/data-sheets/adxl345.pdf)

6. [ADXL345 Datasheet and Product Info | Analog Devices](https://www.analog.com/en/products/adxl345.html) - The ADXL345 is a small, thin, low power, 3-axis accelerometer with high resolution (13-bit) measurem...

7. [AN-1077: ADXL345 Quick Start Guide](https://www.analog.com/en/resources/app-notes/an-1077.html) - This application note discusses the setup of the ADXL345 digital accelerometer.

8. [ADXL345 Product FAQ | Analog Devices](https://www.analog.com/en/resources/product-faqs/ADXL345.html) - I need finer resolution. What do I do? You can always select a sensor with lower noise (Analog Devic...

9. [Preferences - - — Arduino ESP32 latest documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html)

10. [Preferences - - — Arduino ESP32 latest documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)

11. [ADXL345 (Rev. A) - Analog](https://www.analog.com/media/en/pcn/ADI_PCN_10_0327_Rev_A_ADXL345_Rev.%20B.doc)

12. [www.osha.gov › etools › machine-guardingeTool : Machine Guarding - Introduction - General ...](https://www.osha.gov/etools/machine-guarding/introduction/general-requirements)

13. [1910.212 - General requirements for all machines.](http://www.osha.gov/laws-regs/regulations/standardnumber/1910/1910.212) - One or more methods of machine guarding shall be … rotating parts, flying chips and sparks. Examples...

14. [Server - websockets 17.0.1 documentation - Read the Docs](https://websockets.readthedocs.io/en/stable/faq/server.html) - Why does the server close the connection prematurely?: Your connection handler exits prematurely. Wa...

15. [Part 1 - Send & receive - websockets 12.0 documentation](https://websockets.readthedocs.io/en/12.0/intro/tutorial1.html) - In this tutorial, you’re going to build a web-based Connect Four game. The web removes the constrain...

16. [Configuration | PySerial Docs](https://www.pyserial.com/docs/configuration) - Configure PySerial port settings including baud rate, data bits, parity, stop bits, flow control, ti...

17. [pyserial_api.rst.txt](https://pyserial.readthedocs.io/en/latest/_sources/pyserial_api.rst.txt)

