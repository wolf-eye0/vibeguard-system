import { csvCell } from './formatters.js';
export function exportSession(state){
  const headers=['source','received_at','sample_id','state','sensor_ok','vector_rms_g','x_rms_g','y_rms_g','z_rms_g','peak_vector_g','crest_factor','baseline_rms_g','warning_rms_g','alarm_rms_g'];
  const rows=state.samples.map(s=>[s.source,new Date(s.receivedAtMs).toISOString(),s.sampleId,s.state,s.sensorOk,s.rms.vectorG,s.rms.xG,s.rms.yG,s.rms.zG,s.peakVectorG,s.crestFactor,s.baselineRmsG,s.warningRmsG,s.alarmRmsG]);
  const csv=[headers,...rows].map(row=>row.map(csvCell).join(',')).join('\n');
  const blob=new Blob([csv],{type:'text/csv;charset=utf-8'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=`vibeguard-${state.mode.toLowerCase()}-${new Date().toISOString().replaceAll(':','-')}.csv`;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);
}
