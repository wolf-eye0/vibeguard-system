import { CONFIG, stateMeta } from './config.js';

function initialState(mode) {
  return {
    schemaVersion: CONFIG.schemaVersion, mode, asset: CONFIG.asset, hardware: CONFIG.hardware,
    connection: { browserToBridge: mode === 'PREVIEW' ? 'CONNECTED' : 'CONNECTING', bridgeToSerial: mode === 'PREVIEW' ? 'PREVIEW' : 'UNKNOWN', sensor: mode === 'PREVIEW' ? 'PREVIEW' : 'UNKNOWN', endpoint: CONFIG.websocketUrl, lastTelemetryMs: null, dataAgeMs: null, latencyMs: null },
    thresholds: { ...CONFIG.thresholds, updatedAtMs: Date.now() }, latest: null, samples: [], events: [],
    capabilities: { canCalibrate:false, canForceAlarm:false, canClearAlarm:false, canSetThreshold:false, canRunLedTest:false, canExport:true },
    ui: { ...CONFIG.ui }, rejectedPackets: 0, sessionStartedMs: Date.now()
  };
}

export class SessionStore {
  constructor(mode) { this.state = initialState(mode); this.listeners = new Set(); this.priorState = null; }
  subscribe(fn) { this.listeners.add(fn); fn(this.state); return () => this.listeners.delete(fn); }
  notify(reason) { for (const fn of this.listeners) fn(this.state, reason); }
  patch(patch, reason='patch') { Object.assign(this.state, patch); this.notify(reason); }
  setConnection(patch) { Object.assign(this.state.connection, patch); this.notify('connection'); }
  setCapabilities(capabilities) { Object.assign(this.state.capabilities, capabilities); this.notify('capabilities'); }
  ingest(sample) {
    if (!sample?.quality?.valid) {
      if (sample?.state === 'SENSOR_FAULT') {
        const prior = this.state.latest;
        this.state.latest = sample;
        this.state.connection.lastTelemetryMs = sample.receivedAtMs;
        this.state.connection.dataAgeMs = 0;
        this.state.connection.sensor = 'FAULT';
        if (prior?.state !== 'SENSOR_FAULT') this.addEvent({ type:'sensor_fault', severity:'critical', title:'ADXL345 sensor fault', detail:'Measurement values are unavailable. Check power, ground and SPI wiring.', fromState:prior?.state, toState:'SENSOR_FAULT', sampleId:sample.sampleId }, false);
        this.notify('sample');
        return;
      }
      this.state.rejectedPackets += 1;
      if (this.state.rejectedPackets % 5 === 0) this.addEvent({ type:'packet_rejected', severity:'info', title:'Telemetry packets rejected', detail:`${this.state.rejectedPackets} malformed or incomplete packets were ignored.` });
      return;
    }
    const prior = this.state.latest;
    this.state.latest = sample;
    this.state.thresholds = { ...this.state.thresholds, baselineRmsG:sample.baselineRmsG, warningRmsG:sample.warningRmsG, alarmRmsG:sample.alarmRmsG, source:sample.source === 'LIVE' ? 'DEVICE' : 'PREVIEW_CONFIG', updatedAtMs:sample.receivedAtMs };
    this.state.connection.lastTelemetryMs = sample.receivedAtMs;
    this.state.connection.dataAgeMs = 0;
    this.state.connection.sensor = sample.sensorOk ? 'CONNECTED' : 'FAULT';
    this.state.samples.push(sample);
    if (this.state.samples.length > this.state.ui.maxSessionSamples) this.state.samples.splice(0, this.state.samples.length - this.state.ui.maxSessionSamples);
    if (prior && prior.state !== sample.state) {
      const meta = stateMeta[sample.state] || stateMeta.UNKNOWN;
      this.addEvent({ type:'state_changed', severity:meta.severity, title:`Condition changed to ${meta.label}`, detail:`Transition from ${stateMeta[prior.state]?.label || prior.state} to ${meta.label}.`, fromState:prior.state, toState:sample.state, sampleId:sample.sampleId, source:sample.source }, false);
    }
    this.notify('sample');
  }
  addEvent(event, notify=true) {
    this.state.events.unshift({ eventId:event.eventId || `evt-${Date.now()}-${this.state.events.length}`, timestampMs:event.timestampMs || Date.now(), source:this.state.mode, ...event });
    if (this.state.events.length > 500) this.state.events.length = 500;
    if (notify) this.notify('event');
  }
  clearView() { this.state.samples = []; this.state.events = []; this.state.latest = null; this.priorState = null; this.notify('clear'); }
  setUi(patch) { Object.assign(this.state.ui, patch); this.notify('ui'); }
  tick(now=Date.now()) { this.state.connection.dataAgeMs = this.state.connection.lastTelemetryMs ? now - this.state.connection.lastTelemetryMs : null; this.notify('tick'); }
}
