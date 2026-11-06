export const CONFIG = Object.freeze({
  schemaVersion: '1.0.0',
  asset: { id: 'motor-rig-01', name: 'Motor Rig 01', description: 'N20 geared DC motor vibration demonstrator', location: 'Presentation bench' },
  hardware: {
    controller: { model: 'ESP32 DevKit', serialBaud: 115200, usbInterface: 'CP2102' },
    sensor: { model: 'ADXL345', deviceId: '0xE5', interface: 'SPI_4WIRE', rangeG: 16, scaleMgPerLsb: 3.90625, outputDataRateHz: 800, sampleWindow: 128, windowDurationMs: 160, spiHz: 1000000 },
    pins: { cs: 21, sck: 18, miso: 19, mosi: 23, int1: 4, ledGreen: 25, ledBlue: 26, ledRed: 27, bootButton: 0 },
    motor: { type: 'N20 geared DC motor', supplyV: 12, measuredSpeedAvailable: false }
  },
  thresholds: { mode: 'AUTO', baselineRmsG: 0.745, warningRmsG: 0.800, alarmRmsG: 0.900, source: 'CALIBRATED' },
  ui: { theme: 'dark', timeRangeMs: 60000, displayPaused: false, presentationMode: false, liveFollow: true, tableLimit: 20, maxSessionSamples: 18000, chartFps: 10 },
  websocketUrl: `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`
});

export const STATES = Object.freeze(['BOOTING','CALIBRATING','NORMAL','WARNING','ALARM','SENSOR_FAULT','UNKNOWN']);
export const stateMeta = Object.freeze({
  BOOTING: { label: 'Booting', support: 'Waiting for telemetry', severity: 'info', led: 'BLUE' },
  CALIBRATING: { label: 'Calibrating', support: 'Baseline calibration active', severity: 'info', led: 'BLUE' },
  NORMAL: { label: 'Normal', support: 'Below warning threshold', severity: 'success', led: 'GREEN' },
  WARNING: { label: 'Warning', support: 'Warning threshold exceeded', severity: 'warning', led: 'YELLOW' },
  ALARM: { label: 'Alarm', support: 'Alarm threshold exceeded', severity: 'critical', led: 'RED' },
  SENSOR_FAULT: { label: 'Sensor fault', support: 'ADXL345 reading unavailable', severity: 'critical', led: 'BLUE' },
  UNKNOWN: { label: 'Unknown', support: 'Condition not established', severity: 'info', led: 'UNKNOWN' }
});
