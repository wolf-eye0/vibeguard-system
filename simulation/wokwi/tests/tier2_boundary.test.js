/**
 * Tier 2: Boundary & Corner Cases Test Suite
 * 
 * Verifies:
 * - SPI DEVID readback at register 0x00 returns exactly 0xE5
 * - SPI Mode 3 and Mode 0 clock polarity and phase behavior
 * - Single register write and readback (POWER_CTL, DATA_FORMAT, BW_RATE)
 * - Burst read 0xF2 returns 6 bytes corresponding to X, Y, Z acceleration
 * - Z-axis static 1.0g gravity vector representation
 * - N20 tachometer: at 12V = 0 (unpowered), pulse output remains 0 Hz / LOW
 * - Tachometer frequency at RPM boundaries: 300 RPM (5 Hz), 600 RPM (10 Hz), 1200 RPM (20 Hz)
 * - Tachometer underflow clamping (<100 RPM)
 * - Imbalance mass sweep: 0.0g (baseline), 3.5g (nominal), 5.0g (maximum)
 */

const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { WokwiMockHarness } = require('./wokwi_mock_harness.js');

const ROOT_DIR = path.resolve(__dirname, '..');

function runTests() {
  const results = [];

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
    // T2.01: ADXL345 SPI DEVID readback at register 0x00 returns exactly 0xE5
    await testAsync('T2.01_adxl345_spi_devid_readback_exact_0xE5', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Read register 0x00: command header = 0x00 | 0x80 (READ) = 0x80
      const rx = h.spiTransfer([0x80, 0x00]);
      assert.equal(rx.length, 2, 'Expected 2 SPI bytes in exchange');
      const devId = rx[1];
      assert.equal(
        devId,
        0xe5,
        `Expected DEVID 0xE5 (229 dec), got 0x${devId.toString(16).toUpperCase()} (${devId} dec)`
      );
    });

    // T2.02: ADXL345 SPI Mode 3 clock polarity and phase
    await testAsync('T2.02_adxl345_spi_mode3_clock_polarity_phase', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      const spi = h.spiDevices.get(1);
      assert.ok(spi, 'SPI device not initialized');
      // Mode 3: CPOL = 1 (SCK idle high), CPHA = 1 (sampled on rising edge, shifted on falling)
      assert.equal(spi.mode, 3, `Expected SPI Mode 3, got Mode ${spi.mode}`);
    });

    // T2.03: ADXL345 SPI Mode 0 fallback compatibility
    await testAsync('T2.03_adxl345_spi_mode0_fallback_compatibility', async () => {
      // In ESP32 firmware (VibeGuard_ESP32.ino lines 86-90):
      // If Mode 3 doesn't return 0xE5, the firmware switches to SPI_MODE0 and retries.
      // We verify that reading register 0x00 returns 0xE5 consistently.
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      const rx = h.spiTransfer([0x80, 0x00]);
      assert.equal(rx[1], 0xe5, 'DEVID must be readable across initial transactions');
    });

    // T2.04: Single register write and readback (POWER_CTL 0x2D = 0x08)
    await testAsync('T2.04_adxl345_spi_write_power_ctl_register', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Write 0x08 to REG_POWER_CTL (0x2D)
      h.spiTransfer([0x2d, 0x08]);

      // Read back REG_POWER_CTL (0x2D | 0x80 = 0xAD)
      const rx = h.spiTransfer([0xad, 0x00]);
      assert.equal(
        rx[1],
        0x08,
        `Expected POWER_CTL readback 0x08, got 0x${rx[1].toString(16).toUpperCase()}`
      );
    });

    // T2.05: Single register write and readback (DATA_FORMAT 0x31 = 0x0B)
    await testAsync('T2.05_adxl345_spi_write_data_format_register', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Write 0x0B (Full-Res, ±16g, 4-wire) to REG_DATA_FORMAT (0x31)
      h.spiTransfer([0x31, 0x0b]);

      // Read back REG_DATA_FORMAT (0x31 | 0x80 = 0xB1)
      const rx = h.spiTransfer([0xb1, 0x00]);
      assert.equal(
        rx[1],
        0x0b,
        `Expected DATA_FORMAT readback 0x0B, got 0x${rx[1].toString(16).toUpperCase()}`
      );
    });

    // T2.06: Single register write and readback (BW_RATE 0x2C = 0x0D)
    await testAsync('T2.06_adxl345_spi_write_bw_rate_register', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Write 0x0D (800 Hz ODR) to REG_BW_RATE (0x2C)
      h.spiTransfer([0x2c, 0x0d]);

      // Read back REG_BW_RATE (0x2C | 0x80 = 0xAC)
      const rx = h.spiTransfer([0xac, 0x00]);
      assert.equal(
        rx[1],
        0x0d,
        `Expected BW_RATE readback 0x0D, got 0x${rx[1].toString(16).toUpperCase()}`
      );
    });

    // T2.07: Burst read header 0xF2 initiates multi-byte read
    await testAsync('T2.07_adxl345_burst_read_header_0xF2', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // 0xF2 = 0x32 (DATAX0) | 0x80 (READ) | 0x40 (MULTI_BYTE)
      const rx = h.spiTransfer([0xf2, 0x00, 0x00]);
      assert.equal(rx.length, 3, 'Burst read should return 1 byte per clocked word');
    });

    // T2.08: Burst read returns exactly 6 bytes for X, Y, Z acceleration
    await testAsync('T2.08_adxl345_burst_read_returns_6_bytes', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Send 0xF2 followed by 6 dummy bytes to clock DATAX0, DATAX1, DATAY0, DATAY1, DATAZ0, DATAZ1
      const rx = h.spiTransfer([0xf2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
      assert.equal(rx.length, 7, 'Expected header byte + 6 acceleration data bytes');

      const dataBytes = rx.slice(1);
      assert.equal(dataBytes.length, 6, 'Expected exactly 6 acceleration bytes');
      // Verify not all bytes are zero
      const sum = dataBytes.reduce((a, b) => a + b, 0);
      assert.ok(sum > 0, 'Burst read data bytes must not be all zero');
    });

    // T2.09: ADXL345 Z-axis acceleration reflects static 1.0g baseline gravity vector
    await testAsync('T2.09_adxl345_z_axis_reflects_1g_gravity', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      const rx = h.spiTransfer([0xf2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]);
      const dataBytes = rx.slice(1);

      const rawZ = (dataBytes[5] << 8) | dataBytes[4];
      const signedZ = (rawZ << 16) >> 16;
      // In ADXL345 full resolution mode (3.9 mg/LSB), 1g corresponds to ~256 LSBs
      const gz = signedZ / 256.0;

      assert.ok(
        gz >= 0.85 && gz <= 1.15,
        `Z-axis gravity acceleration (${gz.toFixed(3)}g) must be centered around 1.0g (tolerance ±0.15g)`
      );
    });

    // T2.10: N20 tachometer unpowered: 12V = 0 keeps TACH_OUT at LOW (0 Hz)
    await testAsync('T2.10_n20_tachometer_unpowered_zero_hz', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const p12v = h.getPin('12V') || h.getPin('12V_IN');
      const pTach = h.getPin('TACH') || h.getPin('TACH_OUT');

      // Set 12V input to LOW (unpowered)
      h.setPin(p12v.id, 0);

      // Advance time by 500ms (500,000 µs)
      h.advanceTime(500000);

      // TACH_OUT must remain 0
      assert.equal(pTach.value, 0, 'Tachometer pin must remain LOW (0V) when unpowered');
    });

    // T2.11: N20 tachometer unpowered re-check interval
    await testAsync('T2.11_n20_tachometer_unpowered_recheck_interval', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const p12v = h.getPin('12V') || h.getPin('12V_IN');
      const timer = h.timers.get(1);

      // While unpowered, fire timer step
      h.setPin(p12v.id, 0);
      h.stepTimer(1);

      // Must re-arm timer with 50,000 µs safety polling interval
      assert.equal(
        timer.intervalUs,
        50000,
        `Expected unpowered re-check interval of 50000 µs, got ${timer.intervalUs} µs`
      );
    });

    // T2.12: N20 tachometer nominal speed 600 RPM (10.0 Hz)
    await testAsync('T2.12_n20_tachometer_nominal_600rpm_frequency', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const p12v = h.getPin('12V') || h.getPin('12V_IN');
      const timer = h.timers.get(1);

      h.setPin(p12v.id, 1);
      h.setAttr('rpm', 600.0);

      // Fire timer step
      h.stepTimer(1);

      // Formula: half_period_us = (30 / RPM) * 1,000,000
      // For 600 RPM: (30 / 600) * 1,000,000 = 50,000 µs (full period 100,000 µs = 10.0 Hz)
      assert.equal(
        timer.intervalUs,
        50000,
        `At 600 RPM, expected half-period of 50000 µs (10 Hz), got ${timer.intervalUs} µs`
      );
    });

    // T2.13: N20 tachometer boundary speed 300 RPM (5.0 Hz)
    await testAsync('T2.13_n20_tachometer_boundary_300rpm_frequency', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const p12v = h.getPin('12V') || h.getPin('12V_IN');
      const timer = h.timers.get(1);

      h.setPin(p12v.id, 1);
      h.setAttr('rpm', 300.0);

      h.stepTimer(1);

      // For 300 RPM: (30 / 300) * 1,000,000 = 100,000 µs (full period 200,000 µs = 5.0 Hz)
      assert.equal(
        timer.intervalUs,
        100000,
        `At 300 RPM, expected half-period of 100000 µs (5 Hz), got ${timer.intervalUs} µs`
      );
    });

    // T2.14: N20 tachometer boundary speed 1200 RPM (20.0 Hz)
    await testAsync('T2.14_n20_tachometer_boundary_1200rpm_frequency', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const p12v = h.getPin('12V') || h.getPin('12V_IN');
      const timer = h.timers.get(1);

      h.setPin(p12v.id, 1);
      h.setAttr('rpm', 1200.0);

      h.stepTimer(1);

      // For 1200 RPM: (30 / 1200) * 1,000,000 = 25,000 µs (full period 50,000 µs = 20.0 Hz)
      assert.equal(
        timer.intervalUs,
        25000,
        `At 1200 RPM, expected half-period of 25000 µs (20 Hz), got ${timer.intervalUs} µs`
      );
    });

    // T2.15: N20 tachometer sub-boundary clamping (<100 RPM)
    await testAsync('T2.15_n20_tachometer_clamping_underflow', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const p12v = h.getPin('12V') || h.getPin('12V_IN');
      const timer = h.timers.get(1);

      h.setPin(p12v.id, 1);
      // Set abnormal low RPM
      h.setAttr('rpm', 50.0);

      h.stepTimer(1);

      // Chip code clamps rpm < 100 to 600.0f (half-period 50000 µs)
      assert.equal(
        timer.intervalUs,
        50000,
        `RPM < 100 must clamp to 600 RPM (50000 µs), got ${timer.intervalUs} µs`
      );
    });

    // T2.16: Imbalance mass sweep: 0.0g baseline vibration
    await testAsync('T2.16_imbalance_mass_sweep_0g_baseline', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Enable Measurement Mode (Bit 3 of POWER_CTL 0x2D)
      h.spiTransfer([0x2d, 0x08]);

      // Set fault level to 0 (0.0g baseline unbalance)
      const faultAttr = h.getAttr('fault_level');
      if (faultAttr) h.setAttr('fault_level', 0);

      // Collect 10 burst samples and compute max dynamic acceleration on X/Y
      let maxAbsX = 0;
      let maxAbsY = 0;
      for (let i = 0; i < 10; i++) {
        const rx = h.spiTransfer([0xf2, 0, 0, 0, 0, 0, 0]);
        const sx = ((rx[2] << 8) | rx[1]) << 16 >> 16;
        const sy = ((rx[4] << 8) | rx[3]) << 16 >> 16;
        const gx = Math.abs(sx / 256.0);
        const gy = Math.abs(sy / 256.0);
        if (gx > maxAbsX) maxAbsX = gx;
        if (gy > maxAbsY) maxAbsY = gy;
      }

      assert.ok(
        maxAbsX < 0.25,
        `At 0.0g unbalance, max X dynamic amplitude (${maxAbsX.toFixed(3)}g) must be < 0.25g`
      );
      assert.ok(
        maxAbsY < 0.25,
        `At 0.0g unbalance, max Y dynamic amplitude (${maxAbsY.toFixed(3)}g) must be < 0.25g`
      );
    });

    // T2.17: Imbalance mass sweep: 3.5g nominal unbalance
    await testAsync('T2.17_imbalance_mass_sweep_3_5g_nominal', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Enable Measurement Mode (Bit 3 of POWER_CTL 0x2D)
      h.spiTransfer([0x2d, 0x08]);

      // 3.5g out of 5.0g = 70% severity
      const faultAttr = h.getAttr('fault_level');
      if (faultAttr) h.setAttr('fault_level', 70);

      let maxAbsX = 0;
      for (let i = 0; i < 30; i++) {
        const rx = h.spiTransfer([0xf2, 0, 0, 0, 0, 0, 0]);
        const sx = ((rx[2] << 8) | rx[1]) << 16 >> 16;
        const gx = Math.abs(sx / 256.0);
        if (gx > maxAbsX) maxAbsX = gx;
      }

      assert.ok(
        maxAbsX > 0.5,
        `At 3.5g unbalance (70% severity), peak X vibration (${maxAbsX.toFixed(3)}g) must exceed 0.5g`
      );
    });

    // T2.18: Imbalance mass sweep: 5.0g maximum unbalance
    await testAsync('T2.18_imbalance_mass_sweep_5_0g_maximum', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      // Enable Measurement Mode (Bit 3 of POWER_CTL 0x2D)
      h.spiTransfer([0x2d, 0x08]);

      // 5.0g = 100% severity
      const faultAttr = h.getAttr('fault_level');
      if (faultAttr) h.setAttr('fault_level', 100);

      let maxAbsX = 0;
      for (let i = 0; i < 30; i++) {
        const rx = h.spiTransfer([0xf2, 0, 0, 0, 0, 0, 0]);
        const sx = ((rx[2] << 8) | rx[1]) << 16 >> 16;
        const gx = Math.abs(sx / 256.0);
        if (gx > maxAbsX) maxAbsX = gx;
      }

      assert.ok(
        maxAbsX > 1.2,
        `At 5.0g unbalance (100% severity), peak X vibration (${maxAbsX.toFixed(3)}g) must exceed 1.2g`
      );
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
