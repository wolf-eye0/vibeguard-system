import { CONFIG } from './config.js';

export class PreviewSource {
  constructor() { this.timer=null; this.messageHandler=()=>{}; this.statusHandler=()=>{}; this.started=0; this.index=0; }
  subscribe(onMessage,onStatus){ this.messageHandler=onMessage; this.statusHandler=onStatus; }
  connect(){
    this.started=Date.now(); this.statusHandler({ status:'CONNECTED', serial:'PREVIEW', sensor:'PREVIEW' });
    this.timer=setInterval(()=>this.emit(),200); this.emit();
  }
  disconnect(){ clearInterval(this.timer); }
  sendCommand(){ return Promise.reject(new Error('Hardware commands are disabled in preview mode.')); }
  scenario(second){
    const t=second%76;
    if(t<20)return {state:'NORMAL',base:.039};
    if(t<35)return {state:t<31?'NORMAL':'WARNING',base:.039+(t-20)*.0036};
    if(t<43)return {state:'ALARM',base:.125};
    if(t<58){const v=.125-(t-43)*.0062;return {state:t<48?'ALARM':t<53?'WARNING':'NORMAL',base:Math.max(.037,v)}}
    if(t<64)return {state:'CALIBRATING',base:.020};
    if(t<70)return {state:'SENSOR_FAULT',base:null};
    return {state:'NORMAL',base:.040};
  }
  emit(){
    const elapsed=(Date.now()-this.started)/1000; const s=this.scenario(elapsed); const phase=this.index++;
    if(s.state==='SENSOR_FAULT'){
      this.messageHandler({ sampleId:`preview-${phase}`, source:'PREVIEW', receivedAtMs:Date.now(), state:'SENSOR_FAULT', sensorOk:false, rms:{xG:null,yG:null,zG:null,vectorG:null}, peakVectorG:null,crestFactor:null, baselineRmsG:.0185,warningRmsG:.084,alarmRmsG:.12,led:{color:'BLUE',mode:'BLINK'},quality:{valid:false,stale:false},flags:{demo:true,calibrating:false} }); return;
    }
    const ripple=Math.sin(phase*.31)*.0021+Math.sin(phase*.097)*.0012;
    const vector=Math.max(.008,s.base+ripple); const x=vector*.43+Math.sin(phase*.17)*.001; const y=vector*.71+Math.sin(phase*.11)*.0014; const z=vector*.51+Math.cos(phase*.13)*.0011; const peak=vector*(2.45+Math.sin(phase*.07)*.18);
    this.messageHandler({ sampleId:`preview-${String(phase).padStart(6,'0')}`,source:'PREVIEW',receivedAtMs:Date.now(),deviceTsMs:null,bridgeTsMs:null,state:s.state,sensorOk:true,faultCode:null,rms:{xG:x,yG:y,zG:z,vectorG:vector},mean:{xG:.012,yG:-.006,zG:.998},peakVectorG:peak,crestFactor:peak/vector,baselineRmsG:CONFIG.thresholds.baselineRmsG,warningRmsG:CONFIG.thresholds.warningRmsG,alarmRmsG:CONFIG.thresholds.alarmRmsG,led:{color:s.state==='NORMAL'?'GREEN':s.state==='WARNING'?'YELLOW':s.state==='ALARM'?'RED':'BLUE',mode:s.state==='WARNING'||s.state==='CALIBRATING'?'BLINK':'SOLID'},quality:{valid:true,dataReadyTimeouts:0,stale:false},flags:{demo:true,forcedAlarm:false,calibrating:s.state==='CALIBRATING'} });
  }
}
