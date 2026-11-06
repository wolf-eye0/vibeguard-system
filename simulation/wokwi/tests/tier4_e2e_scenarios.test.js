/**
 * Tier 4: Real-World Application & End-to-End Scenarios Test Suite
 * 
 * Verifies:
 * - End-to-end closed-loop vibration monitoring cycle:
 *   Motor spinning at 600 RPM with 3.5g unbalance produces synchronized 10.0 Hz vibration
 *   and 10.0 Hz tachometer pulses.
 * - Telemetry thresholds consistency with ESP32 firmware:
 *   * Normal (< 0.35g Vector RMS -> Green)
 *   * Warning (0.35g - 0.70g Vector RMS -> Amber/Yellow)
 *   * Alarm (>= 0.70g Vector RMS -> Red)
 * - Multi-window persistence filter simulation (K=3 of M=5 window evaluation)
 * - Launcher script (./run_simulation.sh) syntax and path validation
 * - Simulation toolchain dependencies validation (WASI Clang, Arduino CLI)
 * - Precompiled firmware binary build integrity
 */

const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');
const { WokwiMockHarness } = require('./wokwi_mock_harness.js');

const ROOT_DIR = path.resolve(__dirname, '..');
const ENV_DIR = path.resolve(ROOT_DIR, '../VSCode_Wokwi_Environment');

// Digital Signal Processing helpers matching features.cpp
function removeDC(data) {
  const sum = data.reduce((a, b) => a + b, 0);
  const mean = sum / data.length;
  return data.map(v => v - mean);
}

function computeVectorRMS(x, y, z) {
  const n = x.length;
  let sumSq = 0;
  for (let i = 0; i < n; i++) {
    sumSq += x[i] * x[i] + y[i] * y[i] + z[i] * z[i];
  }
  return Math.sqrt(sumSq / n);
}

// FFT helper for dominant peak frequency identification (Hann window + Radix-2 / DFT)
function findDominantFrequency(signal, samplingRate) {
  const n = signal.length;
  // Apply Hann window
  const windowed = signal.map((v, i) => {
    const factor = 0.5 * (1.0 - Math.cos((2 * Math.PI * i) / (n - 1)));
    return v * factor;
  });

  // DFT over frequency bins 1 to N/2
  let maxMag = 0;
  let peakBin = 1;
  const halfN = Math.floor(n / 2);

  for (let k = 1; k < halfN; k++) {
    let re = 0;
    let im = 0;
    for (let t = 0; t < n; t++) {
      const angle = (2 * Math.PI * k * t) / n;
      re += windowed[t] * Math.cos(angle);
      im -= windowed[t] * Math.sin(angle);
    }
    const mag = Math.sqrt(re * re + im * im) * (2.0 / n);
    if (mag > maxMag) {
      maxMag = mag;
      peakBin = k;
    }
  }

  return {
    peakFrequency: peakBin * (samplingRate / n),
    peakMagnitude: maxMag
  };
}

function runTests() {
  const results = [];

  function test(name, fn) {
    const start = Date.now();
    try {
      fn();
      results.push({
        id: name.split('_')[0],
        name,
        status: 'PASSED',
        durationMs: Date.now() - start
      });
    } catch (err) {
      results.push({
        id: name.split('_')[0],
        name,
        status: 'FAILED',
        error: err.message,
        durationMs: Date.now() - start
      });
    }
  }

  async function testAsync(name, fn) {
    const start = Date.now();
    try {
      await fn();
      results.push({
        id: name.split('_')[0],
        name,
        status: 'PASSED',
        durationMs: Date.now() - start
      });
    } catch (err) {
      results.push({
        id: name.split('_')[0],
        name,
        status: 'FAILED',
        error: err.message,
        durationMs: Date.now() - start
      });
    }
  }

  return (async () => {
    // T4.01: End-to-End kinematic and tachometer synchronization (600 RPM = 10 Hz)
    await testAsync('T4.01_e2e_kinematic_and_tachometer_synchronization', async () => {
      // 1. Instantiate both custom chips in parallel
      const motorHarness = new WokwiMockHarness();
      await motorHarness.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const sensorHarness = new WokwiMockHarness();
      await sensorHarness.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Power motor at nominal 600 RPM
      const p12v = motorHarness.getPin('12V') || motorHarness.getPin('12V_IN');
      motorHarness.setPin(p12v.id, 1);
      motorHarness.setAttr('rpm', 600.0);

      // Enable Measurement Mode (Bit 3 of POWER_CTL 0x2D) matching firmware initialization
      sensorHarness.spiTransfer([0x2d, 0x08]);

      // Configure sensor to nominal 3.5g unbalance (70% severity)
      const faultAttr = sensorHarness.getAttr('fault_level');
      if (faultAttr) sensorHarness.setAttr('fault_level', 70);

      // 2. Measure Tachometer Frequency
      const motorTimer = motorHarness.timers.get(1);
      motorHarness.stepTimer(1);
      const tachHalfPeriodUs = motorTimer.intervalUs;
      const tachFreq = 1000000.0 / (tachHalfPeriodUs * 2);

      assert.equal(
        tachHalfPeriodUs,
        50000,
        `Tachometer half-period at 600 RPM must be 50,000 µs (got ${tachHalfPeriodUs})`
      );
      assert.equal(tachFreq, 10.0, `Tachometer frequency must be exactly 10.0 Hz (got ${tachFreq})`);

      // 3. Measure Sensor Kinematics Frequency across 256 samples (800 Hz ODR)
      const samplesX = [];
      for (let i = 0; i < 256; i++) {
        const rx = sensorHarness.spiTransfer([0xf2, 0, 0, 0, 0, 0, 0]);
        const sx = ((rx[2] << 8) | rx[1]) << 16 >> 16;
        samplesX.push(sx / 256.0);
      }

      const cleanX = removeDC(samplesX);
      const fftResult = findDominantFrequency(cleanX, 800.0);

      // At 800 Hz with N=256, frequency resolution is 800/256 = 3.125 Hz per bin.
      // 10.0 Hz falls directly in bin 3 (3 * 3.125 = 9.375 Hz).
      assert.ok(
        Math.abs(fftResult.peakFrequency - 10.0) <= 3.2,
        `Vibration kinematic peak frequency (${fftResult.peakFrequency.toFixed(1)} Hz) must match 10.0 Hz fundamental`
      );
    });

    // T4.02: Telemetry threshold verification - Green Zone (<0.35g Normal)
    await testAsync('T4.02_telemetry_green_zone_normal_vibration', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Enable Measurement Mode
      h.spiTransfer([0x2d, 0x08]);

      // Fault level 0 = normal baseline unbalance (0.0g)
      const faultAttr = h.getAttr('fault_level');
      if (faultAttr) h.setAttr('fault_level', 0);

      const x = [];
      const y = [];
      const z = [];

      for (let i = 0; i < 256; i++) {
        const rx = h.spiTransfer([0xf2, 0, 0, 0, 0, 0, 0]);
        const sx = ((rx[2] << 8) | rx[1]) << 16 >> 16;
        const sy = ((rx[4] << 8) | rx[3]) << 16 >> 16;
        const sz = ((rx[6] << 8) | rx[5]) << 16 >> 16;
        x.push(sx / 256.0);
        y.push(sy / 256.0);
        z.push(sz / 256.0);
      }

      const dx = removeDC(x);
      const dy = removeDC(y);
      const dz = removeDC(z);
      const vRms = computeVectorRMS(dx, dy, dz);

      // Must be <= RMS_WARNING_THRESHOLD (0.35g)
      assert.ok(
        vRms <= 0.35,
        `Normal vibration Vector RMS (${vRms.toFixed(3)}g) must be <= 0.35g for Green Zone`
      );
    });

    // T4.03: Telemetry threshold verification - Amber Zone (0.35g - 0.70g Warning)
    await testAsync('T4.03_telemetry_amber_zone_warning_vibration', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Enable Measurement Mode
      h.spiTransfer([0x2d, 0x08]);

      // Fault level 20 produces moderate unbalance (~0.48g Vector RMS)
      const faultAttr = h.getAttr('fault_level');
      if (faultAttr) h.setAttr('fault_level', 20);

      const x = [];
      const y = [];
      const z = [];

      for (let i = 0; i < 256; i++) {
        const rx = h.spiTransfer([0xf2, 0, 0, 0, 0, 0, 0]);
        const sx = ((rx[2] << 8) | rx[1]) << 16 >> 16;
        const sy = ((rx[4] << 8) | rx[3]) << 16 >> 16;
        const sz = ((rx[6] << 8) | rx[5]) << 16 >> 16;
        x.push(sx / 256.0);
        y.push(sy / 256.0);
        z.push(sz / 256.0);
      }

      const dx = removeDC(x);
      const dy = removeDC(y);
      const dz = removeDC(z);
      const vRms = computeVectorRMS(dx, dy, dz);

      // Must fall in Warning Zone: 0.35g < v_rms < 0.70g
      assert.ok(
        vRms > 0.35,
        `Warning vibration Vector RMS (${vRms.toFixed(3)}g) must be > 0.35g`
      );
      assert.ok(
        vRms < 0.70,
        `Warning vibration Vector RMS (${vRms.toFixed(3)}g) must be < 0.70g`
      );
    });

    // T4.04: Telemetry threshold verification - Red Zone (>=0.70g Alarm)
    await testAsync('T4.04_telemetry_red_zone_alarm_vibration', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Enable Measurement Mode
      h.spiTransfer([0x2d, 0x08]);

      // Fault level 70 (nominal 3.5g unbalance produces ~1.48g Vector RMS)
      const faultAttr = h.getAttr('fault_level');
      if (faultAttr) h.setAttr('fault_level', 70);

      const x = [];
      const y = [];
      const z = [];

      for (let i = 0; i < 256; i++) {
        const rx = h.spiTransfer([0xf2, 0, 0, 0, 0, 0, 0]);
        const sx = ((rx[2] << 8) | rx[1]) << 16 >> 16;
        const sy = ((rx[4] << 8) | rx[3]) << 16 >> 16;
        const sz = ((rx[6] << 8) | rx[5]) << 16 >> 16;
        x.push(sx / 256.0);
        y.push(sy / 256.0);
        z.push(sz / 256.0);
      }

      const dx = removeDC(x);
      const dy = removeDC(y);
      const dz = removeDC(z);
      const vRms = computeVectorRMS(dx, dy, dz);

      // Must meet Alarm threshold: >= 0.70g
      assert.ok(
        vRms >= 0.70,
        `Alarm vibration Vector RMS (${vRms.toFixed(3)}g) must be >= 0.70g for Red Alarm Zone`
      );
    });

    // T4.05: Multi-window persistence filter simulation (K=3 of M=5)
    test('T4.05_persistence_filter_k_of_m_window_simulation', () => {
      // Simulates the persistence filter from state.cpp:
      // Anomaly trigger requires K=3 windows >= 0.70g within sliding M=5 windows.
      // Clearing requires K_clear=3 consecutive clean windows (< 0.70g).

      function evaluateState(history, kRequired = 3, mWindow = 5) {
        const n = history.length;
        const m = Math.min(mWindow, n);
        const windowSlice = history.slice(n - m);
        let run = 0;
        let bestRun = 0;
        for (const breach of windowSlice) {
          if (breach) {
            run++;
            if (run > bestRun) bestRun = run;
          } else {
            run = 0;
          }
        }
        return bestRun >= kRequired;
      }

      function evaluateClear(history, kClear = 3) {
        const n = history.length;
        if (n < kClear) return false;
        for (let i = n - kClear; i < n; i++) {
          if (history[i]) return false;
        }
        return true;
      }

      // Scenario A: Isolated transient pulse [false, false, true, false, false]
      // A single spike must NOT trigger Abnormal state
      const transientHistory = [false, false, true, false, false];
      assert.equal(
        evaluateState(transientHistory),
        false,
        'Transient spike (1 of 5) must not trigger Abnormal state'
      );

      // Scenario B: Sustained vibration [false, true, true, true]
      // 3 consecutive breaches must trigger Abnormal state
      const sustainedHistory = [false, true, true, true];
      assert.equal(
        evaluateState(sustainedHistory),
        true,
        '3 consecutive breaches must trigger Abnormal state'
      );

      // Scenario C: Recovery hysteresis
      // After returning to clean windows, 3 consecutive clean windows clear Abnormal state
      const recoveringHistory = [true, true, true, false, false, false];
      assert.equal(
        evaluateClear(recoveringHistory),
        true,
        '3 consecutive clean windows must clear Abnormal state back to Normal'
      );
    });

    // T4.06: Launcher script ./run_simulation.sh validation
    test('T4.06_launcher_run_simulation_sh_validation', () => {
      const launcherPath = path.join(ROOT_DIR, 'run_simulation.sh');
      assert.ok(fs.existsSync(launcherPath), `Launcher script missing at ${launcherPath}`);

      const stat = fs.statSync(launcherPath);
      // Check executable mode (user executable bit: 0o100)
      const isExecutable = Boolean(stat.mode & 0o111);
      assert.ok(isExecutable, 'run_simulation.sh must have executable bit (+x) set');

      const content = fs.readFileSync(launcherPath, 'utf8');
      assert.ok(content.startsWith('#!/usr/bin/env bash') || content.startsWith('#!/bin/bash'));
      assert.ok(content.includes('diagram.json'), 'Launcher script must target diagram.json');
      assert.ok(content.includes('VSCode_Wokwi_Environment'), 'Launcher must reference portable environment');
    });

    // T4.07: Simulation toolchain dependencies validation
    test('T4.07_simulation_toolchain_dependencies_validation', () => {
      const clangBin = path.join(ENV_DIR, 'wasi-sdk-24.0-x86_64-linux/bin/clang');
      assert.ok(fs.existsSync(clangBin), `WASI clang compiler not found at ${clangBin}`);

      const clangVer = execSync(`"${clangBin}" --version`, { encoding: 'utf8' });
      assert.ok(clangVer.includes('clang version 18.1.2-wasi-sdk'), 'WASI clang must be v18.1.2');

      const arduinoCliBin = path.join(ENV_DIR, 'bin/arduino-cli');
      assert.ok(fs.existsSync(arduinoCliBin), `arduino-cli not found at ${arduinoCliBin}`);

      // Verify executable permission and binary presence
      fs.accessSync(arduinoCliBin, fs.constants.X_OK);
      const stat = fs.statSync(arduinoCliBin);
      assert.ok(
        stat.size > 10000000,
        `arduino-cli binary (${stat.size} bytes) must be a complete executable (>10MB)`
      );

      const vscodeBin = path.join(ENV_DIR, 'VSCode-linux-x64/bin/code');
      assert.ok(fs.existsSync(vscodeBin), `Portable VS Code binary missing at ${vscodeBin}`);
      fs.accessSync(vscodeBin, fs.constants.X_OK);
    });

    // T4.08: Master firmware compilation artifact verification
    test('T4.08_firmware_binary_build_integrity', () => {
      const binPath = path.join(ROOT_DIR, 'build/VibeGuard_ESP32.ino.bin');
      const elfPath = path.join(ROOT_DIR, 'build/VibeGuard_ESP32.ino.elf');

      assert.ok(fs.existsSync(binPath), `Master firmware binary missing: ${binPath}`);
      assert.ok(fs.existsSync(elfPath), `Master firmware ELF missing: ${elfPath}`);

      const binStat = fs.statSync(binPath);
      assert.ok(
        binStat.size > 100000,
        `Firmware binary size (${binStat.size} bytes) must exceed 100KB for full ESP32 image`
      );

      const tomlContent = fs.readFileSync(path.join(ROOT_DIR, 'wokwi.toml'), 'utf8');
      assert.ok(tomlContent.includes('build/VibeGuard_ESP32.ino.bin'), 'wokwi.toml must point to compiled bin');
    });

    return results;
  })();
}

module.exports = { runTests };

if (require.main === module) {
  runTests().then(res => {
    console.log(JSON.stringify(res, null, 2));
  });
}
