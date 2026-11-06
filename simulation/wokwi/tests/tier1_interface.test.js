/**
 * Tier 1: Feature & Interface Coverage Test Suite
 * 
 * Verifies:
 * - WASM exports (chipInit, __wokwi_api_version_1) for custom chips
 * - JSON schema compliance of chip manifests (pins, controls, display)
 * - 8-pin layout of GY-291 ADXL345 (GND, VCC, CS, INT1, INT2, SDO, SDA, SCL)
 * - N20 motor pins (12V_IN, GND, TACH_OUT) and sliders (rpm, imbalance)
 * - 12V power supply module pins and controls
 * - C simulation engine pin & attribute registrations
 * - wokwi.toml configuration and binary references
 */

const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const { WokwiMockHarness, PIN_MODES } = require('./wokwi_mock_harness.js');

const ROOT_DIR = path.resolve(__dirname, '..');
const CHIP_SCHEMA_PATH = path.resolve(
  ROOT_DIR,
  '../VSCode_Wokwi_Environment/extensions/wokwi.wokwi-vscode-3.7.0/schemas/chip.schema.json'
);

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
    // T1.01: ADXL345 WASM exports chipInit
    await testAsync('T1.01_adxl345_wasm_exports_chipInit', async () => {
      const wasmPath = path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm');
      assert.ok(fs.existsSync(wasmPath), `File not found: ${wasmPath}`);
      const bytes = fs.readFileSync(wasmPath);
      const mod = new WebAssembly.Module(bytes);
      const exports = WebAssembly.Module.exports(mod);
      const chipInitExp = exports.find(e => e.name === 'chipInit');
      assert.ok(chipInitExp, "Missing exported function 'chipInit'");
      assert.equal(chipInitExp.kind, 'function');
    });

    // T1.02: ADXL345 WASM exports __wokwi_api_version_1 returning 1
    await testAsync('T1.02_adxl345_wasm_exports_wokwi_api_version', async () => {
      const wasmPath = path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm');
      const bytes = fs.readFileSync(wasmPath);
      const mod = new WebAssembly.Module(bytes);
      const exports = WebAssembly.Module.exports(mod);
      const verExp = exports.find(e => e.name === '__wokwi_api_version_1');
      assert.ok(verExp, "Missing exported function '__wokwi_api_version_1'");

      const h = new WokwiMockHarness();
      await h.loadChip(wasmPath, path.join(ROOT_DIR, 'adxl345-vibe.chip.json'));
      assert.equal(h.instance.exports.__wokwi_api_version_1(), 1, 'API version must return 1');
    });

    // T1.03: N20 Motor WASM exports chipInit
    await testAsync('T1.03_n20_motor_wasm_exports_chipInit', async () => {
      const wasmPath = path.join(ROOT_DIR, 'n20-motor.chip.wasm');
      assert.ok(fs.existsSync(wasmPath), `File not found: ${wasmPath}`);
      const bytes = fs.readFileSync(wasmPath);
      const mod = new WebAssembly.Module(bytes);
      const exports = WebAssembly.Module.exports(mod);
      const chipInitExp = exports.find(e => e.name === 'chipInit');
      assert.ok(chipInitExp, "Missing exported function 'chipInit'");
      assert.equal(chipInitExp.kind, 'function');
    });

    // T1.04: N20 Motor WASM exports __wokwi_api_version_1 returning 1
    await testAsync('T1.04_n20_motor_wasm_exports_wokwi_api_version', async () => {
      const wasmPath = path.join(ROOT_DIR, 'n20-motor.chip.wasm');
      const bytes = fs.readFileSync(wasmPath);
      const mod = new WebAssembly.Module(bytes);
      const exports = WebAssembly.Module.exports(mod);
      const verExp = exports.find(e => e.name === '__wokwi_api_version_1');
      assert.ok(verExp, "Missing exported function '__wokwi_api_version_1'");

      const h = new WokwiMockHarness();
      await h.loadChip(wasmPath, path.join(ROOT_DIR, 'n20-motor.chip.json'));
      assert.equal(h.instance.exports.__wokwi_api_version_1(), 1, 'API version must return 1');
    });

    // T1.05: ADXL345 manifest JSON schema compliance
    test('T1.05_adxl345_manifest_json_schema_compliance', () => {
      const manifestPath = path.join(ROOT_DIR, 'adxl345-vibe.chip.json');
      assert.ok(fs.existsSync(manifestPath), `Manifest not found: ${manifestPath}`);
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));

      assert.equal(typeof manifest.name, 'string', 'Manifest must have a string name');
      assert.ok(manifest.name.length > 0, 'Name must not be empty');
      assert.ok(Array.isArray(manifest.pins), 'Manifest must declare a pins array');
      assert.ok(manifest.pins.length > 0, 'Pins array must not be empty');

      if (manifest.controls) {
        assert.ok(Array.isArray(manifest.controls), 'Controls must be an array');
        for (const c of manifest.controls) {
          assert.match(c.id, /^[a-zA-Z][a-zA-Z0-9]*$/, `Control ID '${c.id}' must be camelCase`);
          assert.ok(['range', 'boolean', 'string'].includes(c.type), `Invalid control type: ${c.type}`);
        }
      }
    });

    // T1.06: N20 Motor manifest JSON schema compliance
    test('T1.06_n20_motor_manifest_json_schema_compliance', () => {
      const manifestPath = path.join(ROOT_DIR, 'n20-motor.chip.json');
      assert.ok(fs.existsSync(manifestPath), `Manifest not found: ${manifestPath}`);
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));

      assert.equal(typeof manifest.name, 'string', 'Manifest must have a string name');
      assert.ok(Array.isArray(manifest.pins), 'Manifest must declare a pins array');
      assert.ok(manifest.pins.length >= 3, 'N20 motor must declare at least 3 pins (12V, GND, TACH)');
      assert.ok(Array.isArray(manifest.controls), 'N20 motor must declare controls');
    });

    // T1.07: ADXL345 manifest has required SPI signal pins
    test('T1.07_adxl345_manifest_has_required_pin_signals', () => {
      const manifestPath = path.join(ROOT_DIR, 'adxl345-vibe.chip.json');
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
      const pins = manifest.pins;

      assert.ok(pins.includes('VCC'), "ADXL345 pins must include 'VCC'");
      assert.ok(pins.includes('GND'), "ADXL345 pins must include 'GND'");
      assert.ok(pins.includes('CS'), "ADXL345 pins must include 'CS'");
      assert.ok(pins.includes('SCK') || pins.includes('SCL'), "ADXL345 pins must include 'SCK' or 'SCL'");
      assert.ok(pins.includes('MOSI') || pins.includes('SDA') || pins.includes('SDI'), "ADXL345 pins must include MOSI/SDA");
      assert.ok(pins.includes('MISO') || pins.includes('SDO'), "ADXL345 pins must include MISO/SDO");
    });

    // T1.08: GY-291 physical procurement 8-pin layout validation
    test('T1.08_adxl345_manifest_gy291_8pin_layout', () => {
      const manifestPath = path.join(ROOT_DIR, 'adxl345-vibe.chip.json');
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
      const expected8Pins = ['GND', 'VCC', 'CS', 'INT1', 'INT2', 'SDO', 'SDA', 'SCL'];

      // Note: Requirement R1 calls for standard 8-pin 0.1" header layout matching Robocraze GY-291
      // If the current manifest has 11 pins, this assertion reports the discrepancy for Milestone M1
      assert.equal(
        manifest.pins.length,
        8,
        `Expected exactly 8 pins for authentic GY-291 breakout, found ${manifest.pins.length}: [${manifest.pins.join(', ')}]`
      );
      assert.deepEqual(manifest.pins, expected8Pins, 'Pins must match physical GY-291 8-pin SIP order');
    });

    // T1.09: N20 Motor pins validation (12V_IN/12V, GND, TACH_OUT/TACH)
    test('T1.09_n20_motor_manifest_pins', () => {
      const manifestPath = path.join(ROOT_DIR, 'n20-motor.chip.json');
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
      const pins = manifest.pins;

      const has12v = pins.includes('12V_IN') || pins.includes('12V');
      const hasGnd = pins.includes('GND');
      const hasTach = pins.includes('TACH_OUT') || pins.includes('TACH');

      assert.ok(has12v, "Motor pins must include '12V_IN' or '12V' alias");
      assert.ok(hasGnd, "Motor pins must include 'GND'");
      assert.ok(hasTach, "Motor pins must include 'TACH_OUT' or 'TACH' alias");
    });

    // T1.10: N20 Motor sliders validation: rpm range control
    test('T1.10_n20_motor_manifest_rpm_control', () => {
      const manifestPath = path.join(ROOT_DIR, 'n20-motor.chip.json');
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
      const rpmCtrl = manifest.controls.find(c => c.id === 'rpm');

      assert.ok(rpmCtrl, "Missing control with id 'rpm'");
      assert.equal(rpmCtrl.type, 'range');
      assert.ok(rpmCtrl.min <= 300, `RPM min (${rpmCtrl.min}) must be <= 300`);
      assert.ok(rpmCtrl.max >= 1200, `RPM max (${rpmCtrl.max}) must be >= 1200`);
      assert.ok(rpmCtrl.step > 0, 'RPM step must be > 0');
      assert.equal(rpmCtrl.value, 600, `RPM default value must be 600, got ${rpmCtrl.value}`);
    });

    // T1.11: N20 Motor sliders validation: imbalance range control
    test('T1.11_n20_motor_manifest_imbalance_control', () => {
      const manifestPath = path.join(ROOT_DIR, 'n20-motor.chip.json');
      const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
      const imbCtrl = manifest.controls.find(c => c.id === 'imbalance');

      assert.ok(imbCtrl, "Missing control with id 'imbalance'");
      assert.equal(imbCtrl.type, 'range');
      assert.ok(imbCtrl.min <= 0.0, `Imbalance min (${imbCtrl.min}) must be <= 0.0g`);
      assert.ok(imbCtrl.max >= 5.0, `Imbalance max (${imbCtrl.max}) must be >= 5.0g`);
      assert.ok(imbCtrl.step > 0, 'Imbalance step must be > 0');
      assert.equal(imbCtrl.value, 3.5, `Imbalance default value must be 3.5g, got ${imbCtrl.value}`);
    });

    // T1.12: 12V DC power supply module interface
    test('T1.12_pwr_supply_manifest_or_schematic_interface', () => {
      // Verifies either a custom chip manifest exists or diagram parts specify 12V switch & live LED
      const pwrChipJson = path.join(ROOT_DIR, 'pwr-supply-12v.chip.json');
      const diagramPath = path.join(ROOT_DIR, 'diagram.json');
      const diagram = JSON.parse(fs.readFileSync(diagramPath, 'utf8'));

      const hasPwrSwitch = diagram.parts.some(p => p.id === 'pwr_switch' || p.type.includes('switch'));
      const has12vLed = diagram.parts.some(p => p.id === 'led_12v' || p.attrs?.label?.includes('12V'));

      assert.ok(
        fs.existsSync(pwrChipJson) || (hasPwrSwitch && has12vLed),
        '12V Power supply must be implemented as custom chip or dedicated schematic module'
      );
    });

    // T1.13: ADXL345 C API pin initialization
    await testAsync('T1.13_adxl345_c_api_pin_init', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      const csPin = h.getPin('CS');
      const sckPin = h.getPin('SCK') || h.getPin('SCL');
      const mosiPin = h.getPin('MOSI') || h.getPin('SDA') || h.getPin('SDI');
      const misoPin = h.getPin('MISO') || h.getPin('SDO');

      assert.ok(csPin, "CS pin must be initialized via pin_init('CS')");
      assert.ok(sckPin, 'SCK/SCL pin must be initialized via pin_init');
      assert.ok(mosiPin, 'MOSI/SDA pin must be initialized via pin_init');
      assert.ok(misoPin, 'MISO/SDO pin must be initialized via pin_init');

      assert.equal(csPin.mode, PIN_MODES.INPUT_PULLUP, 'CS pin must be configured as INPUT_PULLUP');
    });

    // T1.14: ADXL345 C API SPI device registration specifies Mode 3
    await testAsync('T1.14_adxl345_c_api_spi_init_mode', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      const spi = h.spiDevices.get(1);
      assert.ok(spi, 'SPI device must be registered via spi_init');
      assert.equal(spi.mode, 3, `ADXL345 SPI mode must be 3 (CPOL=1, CPHA=1), got ${spi.mode}`);
    });

    // T1.15: ADXL345 C API registers pin watch on CS
    await testAsync('T1.15_adxl345_c_api_pin_watch_cs', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'adxl345-vibe.chip.wasm'),
        path.join(ROOT_DIR, 'adxl345-vibe.chip.json')
      );

      const csPin = h.getPin('CS');
      const watch = h.pinWatches.get(csPin.id);
      assert.ok(watch, 'CS pin must have an active pin_watch registered');
      assert.ok(
        watch.edge === 3 || watch.edge === 2,
        `CS watch edge must be BOTH (3) or FALLING (2), got ${watch.edge}`
      );
    });

    // T1.16: N20 Motor C API pin initialization
    await testAsync('T1.16_n20_motor_c_api_pin_init', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const p12v = h.getPin('12V') || h.getPin('12V_IN');
      const pGnd = h.getPin('GND');
      const pTach = h.getPin('TACH') || h.getPin('TACH_OUT');

      assert.ok(p12v, "Motor must initialize '12V' or '12V_IN' pin");
      assert.ok(pGnd, "Motor must initialize 'GND' pin");
      assert.ok(pTach, "Motor must initialize 'TACH' or 'TACH_OUT' pin");
      assert.equal(pTach.mode, PIN_MODES.OUTPUT_LOW, 'TACH pin must initialize as OUTPUT_LOW');
    });

    // T1.17: N20 Motor C API timer registration
    await testAsync('T1.17_n20_motor_c_api_timer_init', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const timer = h.timers.get(1);
      assert.ok(timer, 'Motor must initialize pulse timer via timer_init');
      assert.ok(timer.active, 'Pulse timer must be armed and active');
    });

    // T1.18: N20 Motor C API default rpm attribute
    await testAsync('T1.18_n20_motor_c_api_attr_rpm_default', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const rpmAttr = h.getAttr('rpm');
      assert.ok(rpmAttr, "Motor must initialize 'rpm' attribute");
      assert.equal(rpmAttr.value, 600.0, `Default RPM must be 600.0, got ${rpmAttr.value}`);
    });

    // T1.19: N20 Motor C API default imbalance attribute
    await testAsync('T1.19_n20_motor_c_api_attr_imbalance_default', async () => {
      const h = new WokwiMockHarness();
      await h.loadChip(
        path.join(ROOT_DIR, 'n20-motor.chip.wasm'),
        path.join(ROOT_DIR, 'n20-motor.chip.json')
      );

      const imbAttr = h.getAttr('imbalance');
      assert.ok(imbAttr, "Motor must initialize 'imbalance' attribute");
      assert.equal(imbAttr.value, 3.5, `Default imbalance must be 3.5, got ${imbAttr.value}`);
    });

    // T1.20: wokwi.toml syntax and chip declarations
    test('T1.20_wokwi_toml_syntax_and_chip_mapping', () => {
      const tomlPath = path.join(ROOT_DIR, 'wokwi.toml');
      assert.ok(fs.existsSync(tomlPath), `wokwi.toml not found: ${tomlPath}`);
      const content = fs.readFileSync(tomlPath, 'utf8');

      assert.ok(content.includes('[[chip]]'), "wokwi.toml must declare '[[chip]]' entries");
      assert.ok(content.includes('adxl345-vibe'), "wokwi.toml must reference 'adxl345-vibe' chip");
      assert.ok(content.includes('n20-motor'), "wokwi.toml must reference 'n20-motor' chip");
    });

    // T1.21: wokwi.toml binary files exist on disk
    test('T1.21_wokwi_toml_binary_files_exist', () => {
      const tomlPath = path.join(ROOT_DIR, 'wokwi.toml');
      const content = fs.readFileSync(tomlPath, 'utf8');
      const binaryMatches = content.matchAll(/binary\s*=\s*"([^"]+)"/g);

      let count = 0;
      for (const match of binaryMatches) {
        count++;
        const binFile = path.resolve(ROOT_DIR, match[1]);
        assert.ok(fs.existsSync(binFile), `Declared binary file missing: ${match[1]}`);
      }
      assert.ok(count >= 2, `Expected at least 2 [[chip]] binary mappings, found ${count}`);
    });

    // T1.22: Manifest controls schema compliance (camelCase and types)
    test('T1.22_manifest_controls_camelcase_and_types', () => {
      const manifests = ['adxl345-vibe.chip.json', 'n20-motor.chip.json'];
      for (const mFile of manifests) {
        const mPath = path.join(ROOT_DIR, mFile);
        const manifest = JSON.parse(fs.readFileSync(mPath, 'utf8'));
        if (manifest.controls) {
          for (const c of manifest.controls) {
            assert.match(
              c.id,
              /^[a-zA-Z][a-zA-Z0-9]*$/,
              `Control id '${c.id}' in ${mFile} must match ^[a-zA-Z][a-zA-Z0-9]*$`
            );
            assert.ok(['range', 'boolean', 'string'].includes(c.type));
            if (c.type === 'range') {
              assert.equal(typeof c.min, 'number');
              assert.equal(typeof c.max, 'number');
              assert.ok(c.min < c.max, `min (${c.min}) must be strictly less than max (${c.max})`);
            }
          }
        }
      }
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
