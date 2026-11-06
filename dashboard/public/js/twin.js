import { stateMeta } from './config.js';
export function updateTwin(sample, thresholds){
  const stage=document.querySelector('#twinStage'); if(!stage)return;
  const state=sample?.state||'UNKNOWN'; const vector=sample?.rms?.vectorG; const ratio=Number.isFinite(vector)&&thresholds.alarmRmsG?vector/thresholds.alarmRmsG:0;
  stage.dataset.state=state; stage.classList.toggle('running',Boolean(sample?.sensorOk)&&state!=='CALIBRATING');stage.classList.toggle('vibrating',ratio>.65&&state!=='CALIBRATING');
  document.querySelector('#twinState').textContent=stateMeta[state]?.label||'Unknown';document.querySelector('#ledState').textContent=sample?.led?`${sample.led.color} · ${sample.led.mode.toLowerCase()}`:'Unknown';
  const values=sample?.rms;const dominant=values&&[values.xG,values.yG,values.zG].every(Number.isFinite)?['X','Y','Z'][[values.xG,values.yG,values.zG].indexOf(Math.max(values.xG,values.yG,values.zG))]:'—';document.querySelector('#dominantAxis').textContent=dominant;
}
