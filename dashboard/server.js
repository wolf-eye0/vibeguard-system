import express from 'express';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer, WebSocket } from 'ws';
import { SerialPort } from 'serialport';
import { ReadlineParser } from '@serialport/parser-readline';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.PORT || 8765);
const SERIAL_BAUD = Number(process.env.SERIAL_BAUD || 115200);
const requestedPort = process.env.SERIAL_PORT || '/dev/ttyUSB0';
const autoSerial = process.env.AUTO_SERIAL !== 'false';
const app = express();
const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: '/ws' });

app.disable('x-powered-by');
app.use(express.json({ limit: '64kb' }));
app.use('/vendor/chart.js', express.static(path.join(__dirname, 'node_modules/chart.js/dist/chart.umd.js')));
app.use(express.static(path.join(__dirname, 'public'), { extensions: ['html'] }));
app.get('/', (_req, res) => res.redirect('/vibeguard-dashboard.html'));
app.get('/health', (_req, res) => res.json({
  ok: true,
  service: 'vibeguard-bridge',
  websocket: `ws://127.0.0.1:${PORT}/ws`,
  serial: serialState
}));
app.all('/api/cmd', (req, res) => {
  const cmd = (req.query.cmd || req.body?.cmd || '').trim().toUpperCase();
  if (!cmd) return res.status(400).json({ ok: false, error: 'Command missing' });
  if (!serial?.isOpen) return res.status(503).json({ ok: false, error: 'Serial connection unavailable' });
  serial.write(`${cmd}\n`, err => {
    if (err) return res.status(500).json({ ok: false, error: err.message });
    console.log(`[HTTP API] Sent command: ${cmd}`);
    res.json({ ok: true, command: cmd });
  });
});
app.all('/api/cmd/:cmd', (req, res) => {
  const cmd = req.params.cmd.toUpperCase();
  if (!serial?.isOpen) return res.status(503).json({ ok: false, error: 'Serial connection unavailable' });
  serial.write(`${cmd}\n`, err => {
    if (err) return res.status(500).json({ ok: false, error: err.message });
    console.log(`[HTTP API] Sent command: ${cmd}`);
    res.json({ ok: true, command: cmd });
  });
});

const clients = new Set();
const commandMap = Object.freeze({
  CALIBRATE: 'CAL',
  CAL_AUTO: 'CAL',
  DEMO_ALARM: 'FORCE_ALARM',
  CLEAR_ALARM: 'CLEAR_ALARM',
  LED_TEST: 'LED_TEST',
  SET_THRESHOLDS: 'SET_THRESHOLDS',
  FORCE_ALARM: 'FORCE_ALARM',
  CAL_OFF: 'CAL_OFF',
  CAL_RUN: 'CAL_RUN',
  PROFILE_APPROVE: 'PROFILE_APPROVE',
  CAL_CANCEL: 'CAL_CANCEL',
  PROFILE_CLEAR: 'PROFILE_CLEAR',
  STATUS: 'STATUS',
  TEST_ON: 'TEST_ON',
  TEST_OFF: 'TEST_OFF',
  TARE_START: 'TARE_START',
  TARE_STOP: 'TARE_STOP',
  CAL_START: 'CAL_START',
  CAL_STOP: 'CAL_STOP',
  CAL_COMMIT: 'CAL_COMMIT',
  CAL_RESET: 'CAL_RESET'
});
let serial = null;
let serialState = {
  status: 'DISCONNECTED', port: null, baud: SERIAL_BAUD, sensor: 'UNKNOWN',
  lastMessageMs: null, error: null
};

function envelope(type, payload = {}) {
  return JSON.stringify({ schemaVersion: '1.0.0', type, bridgeTsMs: Date.now(), ...payload });
}
function broadcast(type, payload = {}) {
  const data = envelope(type, payload);
  for (const client of clients) if (client.readyState === WebSocket.OPEN) client.send(data);
}
function emitBridgeStatus() { broadcast('bridge_status', { connection: serialState }); }

function parseSerialLine(line) {
  const value = line.trim();
  if (!value) return null;
  try { return JSON.parse(value); } catch { /* tolerate labelled or CSV firmware output */ }
  const pairs = Object.fromEntries([...value.matchAll(/([A-Za-z0-9_]+)\s*[:=]\s*(-?\d+(?:\.\d+)?|[A-Za-z0-9_-]+)/g)].map(m => [m[1], m[2]]));
  const meanMatch = value.match(/mean\s*=\s*\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)/);
  if (meanMatch) {
    pairs.meanX = meanMatch[1];
    pairs.meanY = meanMatch[2];
    pairs.meanZ = meanMatch[3];
  }
  const rmsMatch = value.match(/rms\s*=\s*\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)/);
  if (rmsMatch) {
    pairs.rmsX = rmsMatch[1];
    pairs.rmsY = rmsMatch[2];
    pairs.rmsZ = rmsMatch[3];
  }
  if (pairs.vector && !pairs.vectorRms) pairs.vectorRms = pairs.vector;
  if (pairs.warn && !pairs.warning) pairs.warning = pairs.warn;
  if (pairs.device_ms && !pairs.deviceTsMs) pairs.deviceTsMs = pairs.device_ms;
  if (Object.keys(pairs).length) return { raw: value, fields: pairs };
  const columns = value.split(',').map(part => part.trim());
  if (columns.length >= 8 && columns.every((part, i) => i === 0 || Number.isFinite(Number(part)))) {
    const [state, xRms, yRms, zRms, vectorRms, peak, crest, baseline, warning, alarm] = columns;
    return { state, rms: { xG: +xRms, yG: +yRms, zG: +zRms, vectorG: +vectorRms }, peakVectorG: +peak, crestFactor: +crest, baselineRmsG: +baseline, warningRmsG: +warning, alarmRmsG: +alarm };
  }
  return { raw: value };
}

async function choosePort() {
  if (requestedPort) return requestedPort;
  if (!autoSerial) return null;
  const ports = await SerialPort.list();
  const likely = ports.find(p => /CP210|USB|UART|CH340/i.test(`${p.manufacturer || ''} ${p.friendlyName || ''} ${p.productId || ''}`));
  return likely?.path || null;
}

let reconnectTimer = null;
function scheduleReconnect() {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(async () => {
    reconnectTimer = null;
    if (!serial?.isOpen) {
      console.log('[SERIAL] Attempting reconnection...');
      try { await connectSerial(); } catch {}
    }
  }, 2000);
}

async function connectSerial() {
  const portPath = await choosePort();
  if (!portPath) {
    serialState = { ...serialState, status: 'DISCONNECTED', error: 'No serial port configured' };
    scheduleReconnect();
    return;
  }
  serialState = { ...serialState, status: 'CONNECTING', port: portPath, error: null };
  serial = new SerialPort({ path: portPath, baudRate: SERIAL_BAUD, autoOpen: false });
  const parser = serial.pipe(new ReadlineParser({ delimiter: '\n' }));
  serial.on('open', () => {
    serialState = { ...serialState, status: 'CONNECTED', error: null };
    console.log(`[SERIAL] Connected to ${portPath} @ ${SERIAL_BAUD} baud`);
    emitBridgeStatus();
  });
  serial.on('error', err => {
    serialState = { ...serialState, status: 'FAULT', error: err.message };
    console.error(`[SERIAL ERROR] ${err.message}`);
    emitBridgeStatus();
    scheduleReconnect();
  });
  serial.on('close', () => {
    serialState = { ...serialState, status: 'DISCONNECTED', sensor: 'UNKNOWN' };
    console.log('[SERIAL] Disconnected - will auto-reconnect in 2s');
    emitBridgeStatus();
    scheduleReconnect();
  });
  let packetCount = 0;
  parser.on('data', line => {
    const trimmed = line.trim();
    if (trimmed.startsWith('[CAL') || trimmed.startsWith('[STATUS') || trimmed.startsWith('[COMMAND') || trimmed.startsWith('[RESET') || trimmed.startsWith('[EVENT') || trimmed.startsWith('[LIVE')) {
      console.log(trimmed);
      if (trimmed.startsWith('[CAL_STREAM]') || trimmed.startsWith('[COMMAND]')) {
        broadcast('bridge_event', { text: trimmed });
      }
      return;
    }
    if (trimmed.includes('sensor=OK') || trimmed.includes('[DATA]') || trimmed.startsWith('>VRMS:')) {
      if (serialState.sensor !== 'CONNECTED') {
        serialState.sensor = 'CONNECTED';
        emitBridgeStatus();
      }
    }
    const packet = parseSerialLine(line);
    if (!packet) return;
    serialState.lastMessageMs = Date.now();
    broadcast('telemetry', { packet });
    if (++packetCount % 50 === 0) {
      const v = packet.fields?.vectorRms || packet.fields?.vector || packet.rms?.vectorG || 'ok';
      console.log(`[TELEMETRY] Broadcast ${packetCount} samples. Vector RMS: ${v} g`);
    }
  });
  serial.open(err => {
    if (err) {
      serialState = { ...serialState, status: 'FAULT', error: err.message };
      console.error(`[SERIAL OPEN ERROR] ${err.message}`);
      emitBridgeStatus();
      scheduleReconnect();
    }
  });
}

function sendCommand(message, socket) {
  const command = commandMap[message.command];
  const requestId = message.requestId || crypto.randomUUID();
  if (!command) {
    socket.send(envelope('command_result', { requestId, ok: false, error: 'Unsupported command' }));
    return;
  }
  if (!serial?.isOpen) {
    socket.send(envelope('command_result', { requestId, ok: false, error: 'Serial connection unavailable' }));
    return;
  }
  let wire = command;
  if (command === 'CAL_START') {
    const stage = message.payload?.stage ?? 0;
    wire = `CAL_START ${stage}`;
  } else if (command === 'SET_THRESHOLDS') {
    const warning = Number(message.payload?.warningRmsG);
    const alarm = Number(message.payload?.alarmRmsG);
    if (!(warning > 0 && alarm > warning)) {
      socket.send(envelope('command_result', { requestId, ok: false, error: 'Thresholds must satisfy 0 < warning < alarm' }));
      return;
    }
    wire = `${command} ${warning.toFixed(5)} ${alarm.toFixed(5)}`;
  }
  serial.write(`${wire}\n`, err => socket.send(envelope('command_result', {
    requestId, ok: !err, command, error: err?.message || null
  })));
}

wss.on('connection', socket => {
  clients.add(socket);
  socket.send(envelope('hello', {
    service: 'vibeguard-bridge',
    connection: serialState,
    capabilities: {
      canCalibrate: Boolean(serial?.isOpen), canForceAlarm: Boolean(serial?.isOpen),
      canClearAlarm: Boolean(serial?.isOpen), canSetThreshold: Boolean(serial?.isOpen),
      canRunLedTest: Boolean(serial?.isOpen), canExport: true
    }
  }));
  socket.on('message', raw => {
    try {
      const message = JSON.parse(raw.toString());
      if (message.type === 'command') sendCommand(message, socket);
      if (message.type === 'ping') socket.send(envelope('pong', { requestTsMs: message.timestampMs }));
    } catch {
      socket.send(envelope('error', { error: 'Invalid JSON message' }));
    }
  });
  socket.on('close', () => clients.delete(socket));
});

server.listen(PORT, '0.0.0.0', async () => {
  console.log(`VibeGuard: http://127.0.0.1:${PORT}/vibeguard-dashboard.html`);
  try { await connectSerial(); } catch (error) { console.error('Serial setup failed:', error.message); }
});
