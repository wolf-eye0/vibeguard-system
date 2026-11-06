/**
 * Tier 3: Cross-Feature & Hardware Interconnect Test Suite
 * 
 * Verifies:
 * - Schematic netlist integrity in diagram.json
 * - diagram.json schema compliance (diagram.schema.json)
 * - 4-Wire SPI wiring: CS->GPIO21 (bypassing strapping pin GPIO5), SCK->GPIO18, MISO->GPIO19, MOSI->GPIO23, VCC->3.3V, GND->GND
 * - Motor tachometer wiring: TACH_OUT -> GPIO 4
 * - Common-Cathode RGB LED circuit: Green->GPIO25, Blue->GPIO26, Red->GPIO27 via three 220 Ω resistors to COM cathode
 * - 12V power domain isolation: motor 12V supply must not be fed from ESP32 5V logic pin
 */

const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');

const ROOT_DIR = path.resolve(__dirname, '..');
const DIAGRAM_PATH = path.join(ROOT_DIR, 'diagram.json');

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

  function loadDiagram() {
    assert.ok(fs.existsSync(DIAGRAM_PATH), `diagram.json not found at ${DIAGRAM_PATH}`);
    return JSON.parse(fs.readFileSync(DIAGRAM_PATH, 'utf8'));
  }

  function hasNet(diagram, pinA, pinB) {
    return diagram.connections.some(([from, to]) => {
      return (from === pinA && to === pinB) || (from === pinB && to === pinA);
    });
  }

  function findConnections(diagram, pin) {
    return diagram.connections.filter(([from, to]) => from === pin || to === pin);
  }

  // T3.01: diagram.json schema compliance
  test('T3.01_diagram_json_schema_validation', () => {
    const diagram = loadDiagram();
    assert.equal(diagram.version, 1, "diagram.json 'version' must be 1");
    assert.equal(typeof diagram.author, 'string', "diagram.json must have string 'author'");
    assert.equal(typeof diagram.editor, 'string', "diagram.json must have string 'editor'");
    assert.ok(Array.isArray(diagram.parts), "diagram.json must have 'parts' array");
    assert.ok(Array.isArray(diagram.connections), "diagram.json must have 'connections' array");

    for (const part of diagram.parts) {
      assert.equal(typeof part.id, 'string', `Part id must be string: ${JSON.stringify(part)}`);
      assert.equal(typeof part.type, 'string', `Part type must be string: ${JSON.stringify(part)}`);
    }

    for (const conn of diagram.connections) {
      assert.ok(Array.isArray(conn), `Connection must be an array: ${JSON.stringify(conn)}`);
      assert.ok(conn.length >= 3, `Connection must have at least [from, to, color]: ${JSON.stringify(conn)}`);
      assert.match(conn[0], /^.+:.+$/, `Invalid from pin format: ${conn[0]}`);
      assert.match(conn[1], /^.+:.+$/, `Invalid to pin format: ${conn[1]}`);
      assert.equal(typeof conn[2], 'string', `Invalid wire color: ${conn[2]}`);
    }
  });

  // T3.02: Schematic parts list contains ESP32 microcontroller
  test('T3.02_diagram_contains_esp32_devkit', () => {
    const diagram = loadDiagram();
    const espPart = diagram.parts.find(p => p.type === 'board-esp32-devkit-c-v4' || p.id === 'esp');
    assert.ok(espPart, 'Missing ESP32-DevKitC microcontroller board in diagram.json parts');
    assert.equal(espPart.type, 'board-esp32-devkit-c-v4');
  });

  // T3.03: Schematic parts list contains ADXL345 sensor breakout
  test('T3.03_diagram_contains_adxl345_sensor', () => {
    const diagram = loadDiagram();
    const sensorPart = diagram.parts.find(p => p.type.includes('adxl345') || p.id === 'sensor');
    assert.ok(sensorPart, 'Missing ADXL345 sensor custom chip in diagram.json parts');
  });

  // T3.04: Schematic parts list contains N20 motor
  test('T3.04_diagram_contains_n20_motor', () => {
    const diagram = loadDiagram();
    const motorPart = diagram.parts.find(p => p.type.includes('n20') || p.id === 'motor1');
    assert.ok(motorPart, 'Missing N20 metal gear motor in diagram.json parts');
  });

  // T3.05: Schematic parts list contains status RGB LED (common cathode)
  test('T3.05_diagram_contains_rgb_status_led', () => {
    const diagram = loadDiagram();
    const rgbPart = diagram.parts.find(p => p.type === 'wokwi-rgb-led' || p.id === 'status_rgb');
    assert.ok(rgbPart, 'Missing RGB status LED in diagram.json parts');
    assert.equal(
      rgbPart.attrs?.common,
      'cathode',
      `RGB LED must be common cathode, got ${rgbPart.attrs?.common}`
    );
  });

  // T3.06: 4-Wire SPI Hardware Netlist: CS line on GPIO 21 (avoiding strapping pin GPIO5)
  test('T3.06_spi_netlist_cs_gpio21', () => {
    const diagram = loadDiagram();
    const isWired = hasNet(diagram, 'esp:21', 'sensor:CS');
    assert.ok(
      isWired,
      'Hardware SPI Chip Select (CS) must be wired from ESP32 GPIO 21 to sensor:CS (bypassing strapping pin GPIO 5)'
    );
  });

  // T3.07: 4-Wire SPI Hardware Netlist: SCK line on GPIO 18
  test('T3.07_spi_netlist_sck_gpio18', () => {
    const diagram = loadDiagram();
    const isWired = hasNet(diagram, 'esp:18', 'sensor:SCK') || hasNet(diagram, 'esp:18', 'sensor:SCL');
    assert.ok(isWired, 'Hardware SPI Clock (SCK) must be wired from ESP32 GPIO 18 to sensor:SCK/SCL');
  });

  // T3.08: 4-Wire SPI Hardware Netlist: MISO line on GPIO 19
  test('T3.08_spi_netlist_miso_gpio19', () => {
    const diagram = loadDiagram();
    const isWired = hasNet(diagram, 'esp:19', 'sensor:MISO') || hasNet(diagram, 'esp:19', 'sensor:SDO');
    assert.ok(isWired, 'Hardware SPI MISO must be wired from ESP32 GPIO 19 to sensor:MISO/SDO');
  });

  // T3.09: 4-Wire SPI Hardware Netlist: MOSI line on GPIO 23
  test('T3.09_spi_netlist_mosi_gpio23', () => {
    const diagram = loadDiagram();
    const isWired = hasNet(diagram, 'esp:23', 'sensor:MOSI') || hasNet(diagram, 'esp:23', 'sensor:SDA');
    assert.ok(isWired, 'Hardware SPI MOSI must be wired from ESP32 GPIO 23 to sensor:MOSI/SDA');
  });

  // T3.10: 4-Wire SPI Power: sensor VCC connects to ESP32 3V3 rail (3.3V safe)
  test('T3.10_spi_netlist_vcc_3v3_power', () => {
    const diagram = loadDiagram();
    const isWired = hasNet(diagram, 'esp:3V3', 'sensor:VCC');
    assert.ok(isWired, 'Sensor VCC must be connected strictly to ESP32 3V3 rail (not 5V)');
  });

  // T3.11: Common ground continuity: sensor ground connects to ESP32 GND
  test('T3.11_spi_netlist_common_ground', () => {
    const diagram = loadDiagram();
    const isWired =
      hasNet(diagram, 'esp:GND.1', 'sensor:GND') ||
      hasNet(diagram, 'esp:GND.2', 'sensor:GND') ||
      hasNet(diagram, 'esp:GND', 'sensor:GND');
    assert.ok(isWired, 'Sensor GND must connect to common system ground on ESP32 GND');
  });

  // T3.12: Tachometer Netlist: motor tachometer output connects to ESP32 GPIO 4
  test('T3.12_tachometer_netlist_gpio4', () => {
    const diagram = loadDiagram();
    const isWired =
      hasNet(diagram, 'esp:4', 'motor1:TACH') ||
      hasNet(diagram, 'esp:4', 'motor1:TACH_OUT');
    assert.ok(isWired, 'Motor tachometer pulse output must be wired to ESP32 GPIO 4');
  });

  // T3.13: RGB LED Green channel: GPIO 25 connects through 220 Ω resistor to Green anode
  test('T3.13_rgb_led_green_netlist_gpio25_220ohm', () => {
    const diagram = loadDiagram();
    const rGreenPart = diagram.parts.find(p => p.id === 'r_green');
    assert.ok(rGreenPart, "Missing resistor 'r_green' in parts list");
    assert.equal(rGreenPart.attrs?.resistance, '220', `Green resistor must be 220 Ω, got ${rGreenPart.attrs?.resistance}`);

    // Check GPIO 25 -> r_green
    const gpio25Wired =
      hasNet(diagram, 'esp:25', 'r_green:1') || hasNet(diagram, 'esp:25', 'r_green:2');
    assert.ok(gpio25Wired, 'ESP32 GPIO 25 must connect to r_green');

    // Check r_green -> status_rgb:G
    const anodeWired =
      hasNet(diagram, 'r_green:1', 'status_rgb:G') || hasNet(diagram, 'r_green:2', 'status_rgb:G');
    assert.ok(anodeWired, 'r_green output must connect to status_rgb:G');
  });

  // T3.14: RGB LED Blue channel: GPIO 26 connects through 220 Ω resistor to Blue anode
  test('T3.14_rgb_led_blue_netlist_gpio26_220ohm', () => {
    const diagram = loadDiagram();
    const rBluePart = diagram.parts.find(p => p.id === 'r_blue');
    assert.ok(rBluePart, "Missing resistor 'r_blue' in parts list");
    assert.equal(rBluePart.attrs?.resistance, '220', `Blue resistor must be 220 Ω, got ${rBluePart.attrs?.resistance}`);

    // Check GPIO 26 -> r_blue
    const gpio26Wired =
      hasNet(diagram, 'esp:26', 'r_blue:1') || hasNet(diagram, 'esp:26', 'r_blue:2');
    assert.ok(gpio26Wired, 'ESP32 GPIO 26 must connect to r_blue');

    // Check r_blue -> status_rgb:B
    const anodeWired =
      hasNet(diagram, 'r_blue:1', 'status_rgb:B') || hasNet(diagram, 'r_blue:2', 'status_rgb:B');
    assert.ok(anodeWired, 'r_blue output must connect to status_rgb:B');
  });

  // T3.15: RGB LED Red channel: GPIO 27 connects through 220 Ω resistor to Red anode
  test('T3.15_rgb_led_red_netlist_gpio27_220ohm', () => {
    const diagram = loadDiagram();
    const rRedPart = diagram.parts.find(p => p.id === 'r_red');
    assert.ok(rRedPart, "Missing resistor 'r_red' in parts list");
    assert.equal(rRedPart.attrs?.resistance, '220', `Red resistor must be 220 Ω, got ${rRedPart.attrs?.resistance}`);

    // Check GPIO 27 -> r_red
    const gpio27Wired =
      hasNet(diagram, 'esp:27', 'r_red:1') || hasNet(diagram, 'esp:27', 'r_red:2');
    assert.ok(gpio27Wired, 'ESP32 GPIO 27 must connect to r_red');

    // Check r_red -> status_rgb:R
    const anodeWired =
      hasNet(diagram, 'r_red:1', 'status_rgb:R') || hasNet(diagram, 'r_red:2', 'status_rgb:R');
    assert.ok(anodeWired, 'r_red output must connect to status_rgb:R');
  });

  // T3.16: 12V Power Domain Isolation Audit
  test('T3.16_12v_domain_isolation_audit', () => {
    const diagram = loadDiagram();

    // The frozen procurement spec (R3 & R4) specifies that the 12V motor supply rail must be isolated
    // from the ESP32 5V USB logic rail.
    // In diagram.json line 94, "pwr_switch:1" is wired to "esp:5V".
    // This test detects the 5V power hijacking defect and mandates resolution via Milestone M3 / M4.
    const has5vFeedToSwitch = diagram.connections.some(
      ([from, to]) =>
        (from === 'esp:5V' && (to === 'pwr_switch:1' || to.includes('motor'))) ||
        (to === 'esp:5V' && (from === 'pwr_switch:1' || from.includes('motor')))
    );

    assert.ok(
      !has5vFeedToSwitch,
      'ELECTRICAL FAULT: 12V motor power is incorrectly fed from ESP32 5V pin! Must be fed from dedicated 12V supply module.'
    );
  });

  return results;
}

module.exports = { runTests };

if (require.main === module) {
  const res = runTests();
  console.log(JSON.stringify(res, null, 2));
}
