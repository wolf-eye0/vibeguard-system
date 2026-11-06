import { CONFIG, STATES } from './config.js';
import { finite } from './formatters.js';
let sequence = 0;
const pick = (object, ...paths) => {
  for (const path of paths) {
    const value = path.split('.').reduce((acc, key) => acc?.[key], object);
    if (value !== undefined && value !== null && value !== '') return value;
  }
  return null;
};
const normalizedState = value => {
  const candidate = String(value || 'UNKNOWN').replace(/[\[\]]/g, '').trim().toUpperCase().replaceAll(' ','_');
  if (candidate === 'SENSORFAULT' || candidate === 'FAULT') return 'SENSOR_FAULT';
  return STATES.includes(candidate) ? candidate : 'UNKNOWN';
};

export function normalizePacket(packet, source='LIVE', bridgeTsMs=null) {
  const p = packet?.packet || packet || {};
  const fields = p.fields || {};
  const value = (...keys) => pick(p, ...keys) ?? pick(fields, ...keys);
  let xG = finite(value('rms.xG','rmsX','xRms','x','X'));
  let yG = finite(value('rms.yG','rmsY','yRms','y','Y'));
  let zG = finite(value('rms.zG','rmsZ','zRms','z','Z'));
  let vectorG = finite(value('rms.vectorG','vectorRmsG','vectorRms','rms','RMS','vrms','VRMS','vector_rms','vector'));
  if (vectorG === null && [xG,yG,zG].every(Number.isFinite)) vectorG = Math.hypot(xG,yG,zG);
  if (vectorG !== null && (!Number.isFinite(xG) || !Number.isFinite(yG) || !Number.isFinite(zG))) {
    xG = vectorG * 0.577;
    yG = vectorG * 0.577;
    zG = vectorG * 0.577;
  }
  const peakVectorG = finite(value('peakVectorG','peak','peakG','PEAK')) ?? (vectorG !== null ? vectorG * 1.414 : null);
  let crestFactor = finite(value('crestFactor','crest','CREST')) ?? (vectorG !== null ? 1.414 : null);
  if (crestFactor === null && Number.isFinite(peakVectorG) && vectorG > 0) crestFactor = peakVectorG / vectorG;
  const baselineRmsG = finite(value('baselineRmsG','baseline','BASELINE','Baseline')) ?? CONFIG.thresholds.baselineRmsG;
  const warningRmsG = finite(value('warningRmsG','warning','WARN','WarnLimit','warning_thr')) ?? CONFIG.thresholds.warningRmsG;
  const alarmRmsG = finite(value('alarmRmsG','alarm','ALARM','AlarmLimit','alarm_thr')) ?? CONFIG.thresholds.alarmRmsG;
  let state = normalizedState(value('state','State','condition','Condition','STATE'));
  if (state === 'UNKNOWN' && vectorG !== null) {
    state = (vectorG >= alarmRmsG) ? 'ALARM' : ((vectorG >= warningRmsG) ? 'WARNING' : 'NORMAL');
  }
  const sensorOkRaw = value('sensorOk','sensor_ok','sensor');
  const sensorOk = sensorOkRaw === null ? state !== 'SENSOR_FAULT' : !['false','0','fault','offline'].includes(String(sensorOkRaw).toLowerCase());
  const now = Date.now();
  const valid = Number.isFinite(vectorG) && [xG,yG,zG].every(Number.isFinite) && sensorOk;
  return {
    sampleId: String(value('sampleId','id') || `${source.toLowerCase()}-${String(++sequence).padStart(6,'0')}`),
    source, receivedAtMs: now, deviceTsMs: finite(value('deviceTsMs','timestampMs','ts')), bridgeTsMs: finite(bridgeTsMs),
    state: sensorOk ? state : 'SENSOR_FAULT', sensorOk, faultCode:value('faultCode','fault') || null,
    rms:{ xG,yG,zG,vectorG }, mean:{ xG:finite(value('mean.xG','meanX')), yG:finite(value('mean.yG','meanY')), zG:finite(value('mean.zG','meanZ')) },
    peakVectorG, crestFactor, baselineRmsG, warningRmsG, alarmRmsG,
    tareFloorG: finite(value('tareFloorG','tare','TARE','Tare')) ?? 0.0,
    calMode: finite(value('calMode','cal_mode')) ?? -1,
    calSec: finite(value('calSec','cal_sec')) ?? 0.0,
    calCount: finite(value('calCount','cal_count')) ?? 0,
    calAvg: finite(value('calAvg','cal_avg')),
    stage: finite(value('stage_4','stage','Stage','stage4')),
    jVal: finite(value('j_val','j_smooth','j','J')),
    f1Hz: finite(value('f1_hz','f1','F1')),
    led:{ color:String(value('led.color','ledColor') || (state === 'NORMAL' ? 'GREEN' : state === 'WARNING' ? 'YELLOW' : state === 'ALARM' ? 'RED' : 'BLUE')).toUpperCase(), mode:String(value('led.mode','ledMode') || (state === 'ALARM' ? 'STROBE' : 'SOLID')).toUpperCase() },
    quality:{ valid, dataReadyTimeouts:finite(value('quality.dataReadyTimeouts','timeouts')) || 0, stale:false },
    flags:{ demo:Boolean(value('flags.demo','demo')), forcedAlarm:Boolean(value('flags.forcedAlarm','forcedAlarm')), calibrating:state === 'CALIBRATING' }
  };
}
