import { CONFIG,stateMeta } from './config.js';
import { SessionStore } from './store.js';
import { PreviewSource } from './preview-source.js';
import { WebSocketSource } from './websocket-source.js';
import { DashboardCharts } from './charts.js';
import { formatG,formatCrest,formatAge,formatTime,stateLabel } from './formatters.js';
import { exportSession } from './csv-export.js';
import { updateTwin } from './twin.js';
import { openDrawer,closeDrawers,installFocusHandling } from './accessibility.js';

const params=new URLSearchParams(location.search);const mode=params.get('source')==='preview'?'PREVIEW':'LIVE';const store=new SessionStore(mode);const source=mode==='LIVE'?new WebSocketSource(CONFIG.websocketUrl):new PreviewSource();const charts=new DashboardCharts();let renderedTableAt=0;let lastReason='';
const $=selector=>document.querySelector(selector);

function setText(selector,value){const node=$(selector);if(node&&node.textContent!==String(value))node.textContent=value;}
function stateBanner(state){const banner=$('#systemBanner'),latest=state.latest,age=state.connection.dataAgeMs;let spec=null;if(state.mode==='PREVIEW')spec={title:'Preview data is active',message:'Values on this screen are not from the hardware.'};if(state.mode==='LIVE'&&state.connection.browserToBridge!=='CONNECTED')spec={title:'Local bridge unavailable',message:`Unable to reach ${state.connection.endpoint}.`};if(latest?.state==='CALIBRATING')spec={title:'Calibrating baseline',message:'Keep the motor off and the sensor still.'};if(latest?.state==='SENSOR_FAULT')spec={title:'ADXL345 unavailable',message:'Check 3.3 V, ground and SPI wiring.'};if(Number.isFinite(age)&&age>2500)spec={title:'Telemetry is stale',message:`Last valid reading was ${(age/1000).toFixed(1)} seconds ago.`};banner.hidden=!spec;if(spec){setText('#bannerTitle',spec.title);setText('#bannerMessage',spec.message);}}
function renderKpis(state){const s=state.latest,age=state.connection.dataAgeMs,stale=Number.isFinite(age)&&age>2500;const meta=s?stateMeta[s.state]:stateMeta.BOOTING;const condition=$('.condition-cell');condition.dataset.state=s?.state||'BOOTING';setText('#stateValue',stale?`LAST ${meta.label.toUpperCase()}`:meta.label.toUpperCase());setText('#stateSupport',stale?'Telemetry is not current':meta.support);setText('#vectorValue',formatG(s?.rms?.vectorG));setText('#peakValue',formatG(s?.peakVectorG));setText('#crestValue',formatCrest(s?.crestFactor));setText('#trendCurrent',formatG(s?.rms?.vectorG));const warning=s?.warningRmsG??state.thresholds.warningRmsG,alarm=s?.alarmRmsG??state.thresholds.alarmRmsG,vector=s?.rms?.vectorG;const margin=Number.isFinite(vector)?warning-vector:null;setText('#marginValue',formatG(margin));setText('#marginSupport',margin===null?'Until warning':margin>=0?'Until warning':'Warning exceeded');setText('#vectorSupport',Number.isFinite(vector)?`${Math.round(vector/alarm*100)}% of alarm limit`:'Waiting for data');$('#marginTrack').style.width=Number.isFinite(vector)?`${Math.min(100,Math.max(0,vector/alarm*100))}%`:'0%';$('#warningTick').style.left=`${warning/alarm*100}%`;$('#alarmTick').style.left='100%';setText('#dataAge',formatAge(age));setText('#thresholdDetails',`${formatG(warning)} / ${formatG(alarm)} g`);setText('#sourceDetails',state.thresholds.source);setText('#calActiveWarn',formatG(warning));setText('#calActiveAlarm',formatG(alarm));if(s?.tareFloorG!==undefined)setText('#calActiveTare',formatG(s.tareFloorG));if(s?.calMode!==undefined&&s.calMode>=0&&Number.isFinite(s.calAvg)&&s.calCount>0){const activeId=s.calMode===99?'#calTareVal':`#calS${s.calMode}Val`;setText(activeId,`${formatG(s.calAvg)} (${s.calCount})`);}}
function renderConnections(state){const bridge=$('#bridgeStatus'),sensor=$('#sensorStatus');const bridgeOk=state.mode==='PREVIEW'||state.connection.browserToBridge==='CONNECTED';bridge.className=`connection-item ${bridgeOk?'connected':'fault'}`;bridge.innerHTML=`<i class="status-dot"></i>${state.mode==='PREVIEW'?'Preview source':bridgeOk?'Bridge connected':'Bridge offline'}`;const sensorOk=state.mode==='PREVIEW'||state.connection.sensor==='CONNECTED';sensor.className=`connection-item ${sensorOk?'connected':'fault'}`;sensor.innerHTML=`<i class="status-dot"></i>${state.mode==='PREVIEW'?'Sensor preview':sensorOk?'Sensor connected':'Sensor unavailable'}`;setText('#modeBadge',state.mode);}
function renderAxis(state){const s=state.latest;if(!s?.quality.valid){for(const axis of ['x','y','z']){setText(`#${axis}Value`,'—');$(`#${axis}Bar`).style.width='0%';}setText('#axisSummary','Waiting for valid axis data.');return;}const max=Math.max(s.alarmRmsG,s.rms.xG,s.rms.yG,s.rms.zG);for(const axis of ['x','y','z']){const v=s.rms[`${axis}G`];setText(`#${axis}Value`,`${formatG(v)} g`);$(`#${axis}Bar`).style.width=`${Math.min(100,v/max*100)}%`;}const entries=[['X',s.rms.xG],['Y',s.rms.yG],['Z',s.rms.zG]].sort((a,b)=>b[1]-a[1]);setText('#axisSummary',`${entries[0][0]} is the dominant axis in the current DSP window. This is directional evidence, not a fault diagnosis.`);}
function renderEvents(state){const html=state.events.slice(0,8).map(e=>`<li data-severity="${e.severity}"><time datetime="${new Date(e.timestampMs).toISOString()}">${formatTime(e.timestampMs)}</time><div><strong>${escapeHtml(e.title)}</strong><p>${escapeHtml(e.detail||'')}</p></div></li>`).join('')||'<li class="empty-row">Waiting for session events.</li>';$('#eventList').innerHTML=html;$('#allEventList').innerHTML=state.events.map(e=>`<li data-severity="${e.severity}"><time>${formatTime(e.timestampMs)}</time><div><strong>${escapeHtml(e.title)}</strong><p>${escapeHtml(e.detail||'')}</p></div></li>`).join('')||'<li class="empty-row">No session events.</li>';}
function renderTable(state,force=false){const now=performance.now();if(!force&&now-renderedTableAt<200)return;renderedTableAt=now;const rows=state.samples.slice(-state.ui.tableLimit).reverse();$('#readingsBody').innerHTML=rows.map(s=>`<tr><td>${formatTime(s.receivedAtMs)}</td><td><span class="state-badge">${stateLabel(s.state)}</span></td><td>${formatG(s.rms.vectorG)} g</td><td>${formatG(s.rms.xG)} g</td><td>${formatG(s.rms.yG)} g</td><td>${formatG(s.rms.zG)} g</td><td>${formatG(s.peakVectorG)} g</td><td>${formatCrest(s.crestFactor)}</td><td>${formatG(s.warningRmsG)} g</td><td>${formatG(s.alarmRmsG)} g</td></tr>`).join('')||'<tr><td colspan="10" class="empty-row">Waiting for telemetry.</td></tr>';}
function escapeHtml(value){return String(value).replace(/[&<>'"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[c]));}
function render(state,reason){lastReason=reason;renderConnections(state);stateBanner(state);renderKpis(state);renderAxis(state);updateTwin(state.latest,state.thresholds);if(!state.ui.displayPaused)charts.render(state.samples,reason==='clear');if(reason!=='tick'){renderEvents(state);renderTable(state,reason==='clear');}document.body.classList.toggle('presentation',state.ui.presentationMode);setText('#pauseButton', '');$('#pauseButton').innerHTML=state.ui.displayPaused?'<svg viewBox="0 0 24 24"><path d="M8 5l11 7-11 7z"/></svg>':'<svg viewBox="0 0 24 24"><path d="M9 5v14M15 5v14"/></svg>';$('#pauseButton').setAttribute('aria-label',state.ui.displayPaused?'Resume display':'Pause display');const elapsed=Date.now()-state.sessionStartedMs;setText('#sessionClock',`SESSION ${new Date(elapsed).toISOString().slice(11,19)}`);}
function openById(id){openDrawer($(id));}

function bind(){
  $('[data-theme-toggle]').addEventListener('click',()=>{const next=document.documentElement.dataset.theme==='dark'?'light':'dark';document.documentElement.dataset.theme=next;$('[data-theme-toggle]').setAttribute('aria-label',`Switch to ${next==='dark'?'light':'dark'} mode`);requestAnimationFrame(()=>charts.refreshTheme());});
  $('#pauseButton').addEventListener('click',()=>{const next=!store.state.ui.displayPaused;store.setUi({displayPaused:next});store.addEvent({type:'visualization_paused',severity:'info',title:next?'Display paused':'Display resumed',detail:next?'Acquisition continues in the session buffer.':'Charts are following current data again.'});});
  $('#presentationButton').addEventListener('click',()=>store.setUi({presentationMode:!store.state.ui.presentationMode}));
  $('#menuButton').addEventListener('click',()=>openById('#controlDrawer'));$('#hardwareButton').addEventListener('click',()=>openById('#hardwareDrawer'));$('#eventsButton').addEventListener('click',()=>openById('#eventsDrawer'));$('#drawerBackdrop').addEventListener('click',closeDrawers);for(const button of document.querySelectorAll('.close-drawer'))button.addEventListener('click',closeDrawers);
  for(const button of document.querySelectorAll('[data-range]'))button.addEventListener('click',()=>{for(const b of document.querySelectorAll('[data-range]'))b.classList.remove('active');button.classList.add('active');store.setUi({timeRangeMs:Number(button.dataset.range)});charts.setRange(button.dataset.range);});
  $('#exportButton').addEventListener('click',()=>{exportSession(store.state);store.addEvent({type:'export_created',severity:'success',title:'Session CSV exported',detail:`Exported ${store.state.samples.length} ${store.state.mode.toLowerCase()} samples.`});});
  $('#clearButton').addEventListener('click',()=>{$('#confirmDialog').hidden=false;$('#cancelClear').focus();});$('#cancelClear').addEventListener('click',()=>{$('#confirmDialog').hidden=true;$('#clearButton').focus();});$('#confirmClear').addEventListener('click',()=>{store.clearView();$('#confirmDialog').hidden=true;});
  const commandButtons=[...document.querySelectorAll('[data-command]')];for(const button of commandButtons){button.disabled=mode==='PREVIEW';button.addEventListener('click',async()=>{try{setText('#commandFeedback','Sending command…');await source.sendCommand(button.dataset.command);setText('#commandFeedback','Command accepted by the bridge.');}catch(error){setText('#commandFeedback',error.message);}});}
  $('#warningInput').value=CONFIG.thresholds.warningRmsG;$('#alarmInput').value=CONFIG.thresholds.alarmRmsG;$('#thresholdForm button').disabled=mode==='PREVIEW';$('#thresholdForm').addEventListener('submit',async event=>{event.preventDefault();const warningRmsG=Number($('#warningInput').value),alarmRmsG=Number($('#alarmInput').value);if(!(warningRmsG>0&&alarmRmsG>warningRmsG)){setText('#commandFeedback','Alarm must be greater than warning.');return;}try{await source.sendCommand('SET_THRESHOLDS',{warningRmsG,alarmRmsG});setText('#commandFeedback','Threshold update accepted.');}catch(error){setText('#commandFeedback',error.message);}});
  bindFieldCalibration();
  document.addEventListener('keydown',event=>{if(event.key.toLowerCase()==='p'&&!['INPUT','TEXTAREA'].includes(document.activeElement.tagName))store.setUi({presentationMode:!store.state.ui.presentationMode});});installFocusHandling();
}

let activeCalMode = null;
let calTimer = null;
let calSecondsRemaining = 0;
let recordedStages = { tare: null, s0: null, s1: null, s2: null };

function bindFieldCalibration() {
  const btnTare = $('#btnTare');
  const btnS0 = $('#btnCalS0');
  const btnS1 = $('#btnCalS1');
  const btnS2 = $('#btnCalS2');
  const btnCommit = $('#btnCalCommit');
  const btnReset = $('#btnCalReset');
  const feedback = $('#calFeedback');
  const statusBadge = $('#calStatusBadge');

  if (mode === 'PREVIEW') {
    [btnTare, btnS0, btnS1, btnS2, btnCommit, btnReset].forEach(b => { if (b) b.disabled = true; });
    if (feedback) feedback.textContent = 'Field calibration is active in live hardware mode.';
    return;
  }

  const setFeedback = (msg, isErr = false) => {
    if (feedback) {
      feedback.textContent = msg;
      feedback.style.color = isErr ? 'var(--alarm)' : 'var(--text-3)';
    }
  };

  const updateCardState = (cardId, isRecording) => {
    const card = $(cardId);
    if (card) card.classList.toggle('recording', isRecording);
  };

  const startCal = async (modeKey, startCmd, payload, btn, cardId, label) => {
    if (activeCalMode) return;
    activeCalMode = modeKey;
    updateCardState(cardId, true);
    if (statusBadge) {
      statusBadge.textContent = `RECORDING ${label.toUpperCase()}`;
      statusBadge.classList.add('recording');
    }
    setFeedback(`Recording ${label}... Minimum 3s safety floor active.`);
    
    btn.disabled = true;
    btn.classList.add('recording');
    calSecondsRemaining = 3;
    btn.textContent = `Recording... (${calSecondsRemaining}s)`;

    try {
      await source.sendCommand(startCmd, payload);
    } catch (e) {
      setFeedback(`Failed to start: ${e.message}`, true);
      btn.disabled = false;
      btn.classList.remove('recording');
      btn.textContent = `Start ${label}`;
      updateCardState(cardId, false);
      if (statusBadge) {
        statusBadge.textContent = 'IDLE';
        statusBadge.classList.remove('recording');
      }
      activeCalMode = null;
      return;
    }

    calTimer = setInterval(() => {
      calSecondsRemaining--;
      if (calSecondsRemaining > 0) {
        btn.textContent = `Recording... (${calSecondsRemaining}s)`;
      } else {
        clearInterval(calTimer);
        calTimer = null;
        btn.disabled = false;
        btn.textContent = `Stop & Save ${label}`;
      }
    }, 1000);
  };

  const stopCal = async (modeKey, stopCmd, btn, cardId, valId, label, stageNum = null) => {
    if (activeCalMode !== modeKey) return;
    if (calTimer) {
      clearInterval(calTimer);
      calTimer = null;
    }
    btn.disabled = true;
    btn.textContent = 'Computing Mean...';

    try {
      setFeedback(`Finalizing mathematical average across all recorded windows for ${label}...`);
      await source.sendCommand(stopCmd);
    } catch (e) {
      setFeedback(`Failed to stop: ${e.message}`, true);
      btn.disabled = false;
      btn.classList.remove('recording');
      btn.textContent = `Start ${label}`;
      updateCardState(cardId, false);
      activeCalMode = null;
    }
  };

  btnTare?.addEventListener('click', () => {
    if (activeCalMode === null) startCal('TARE', 'TARE_START', {}, btnTare, '#calCardTare', 'Tare');
    else if (activeCalMode === 'TARE') stopCal('TARE', 'TARE_STOP', btnTare, '#calCardTare', '#calTareVal', 'Tare');
  });

  btnS0?.addEventListener('click', () => {
    if (activeCalMode === null) startCal('S0', 'CAL_START', { stage: 0 }, btnS0, '#calCardS0', 'Stage 0');
    else if (activeCalMode === 'S0') stopCal('S0', 'CAL_STOP', btnS0, '#calCardS0', '#calS0Val', 'Stage 0', 0);
  });

  btnS1?.addEventListener('click', () => {
    if (activeCalMode === null) startCal('S1', 'CAL_START', { stage: 1 }, btnS1, '#calCardS1', 'Stage 1');
    else if (activeCalMode === 'S1') stopCal('S1', 'CAL_STOP', btnS1, '#calCardS1', '#calS1Val', 'Stage 1', 1);
  });

  btnS2?.addEventListener('click', () => {
    if (activeCalMode === null) startCal('S2', 'CAL_START', { stage: 2 }, btnS2, '#calCardS2', 'Stage 2');
    else if (activeCalMode === 'S2') stopCal('S2', 'CAL_STOP', btnS2, '#calCardS2', '#calS2Val', 'Stage 2', 2);
  });

  btnCommit?.addEventListener('click', async () => {
    try {
      btnCommit.disabled = true;
      setFeedback('Calculating and saving thresholds to ESP32 Flash memory...');
      await source.sendCommand('CAL_COMMIT');
      setFeedback('Thresholds committed and saved to Flash memory! Monitoring updated.');
      store.addEvent({
        type: 'calibration_committed',
        severity: 'success',
        title: 'Field Calibration Committed',
        detail: 'New threshold corridors computed and saved to ESP32 Flash.'
      });
    } catch (e) {
      setFeedback(`Commit failed: ${e.message}`, true);
      btnCommit.disabled = false;
    }
  });

  btnReset?.addEventListener('click', async () => {
    try {
      setFeedback('Reverting to factory default thresholds...');
      await source.sendCommand('CAL_RESET');
      recordedStages = { tare: null, s0: null, s1: null, s2: null };
      setText('#calTareVal', '—');
      setText('#calS0Val', '—');
      setText('#calS1Val', '—');
      setText('#calS2Val', '—');
      if (btnCommit) btnCommit.disabled = true;
      setFeedback('Factory default thresholds restored.');
      store.addEvent({
        type: 'calibration_reset',
        severity: 'info',
        title: 'Thresholds Reset',
        detail: 'Restored factory default warning (0.80 g) and alarm (0.95 g).'
      });
    } catch (e) {
      setFeedback(`Reset failed: ${e.message}`, true);
    }
  });
}

function handleBridgeEvent(event) {
  const text = event?.text || '';
  const feedback = $('#calFeedback');
  const btnCommit = $('#btnCalCommit');
  const statusBadge = $('#calStatusBadge');
  
  if (text.includes('ack=CAL_STOP ok=1')) {
    const stageMatch = text.match(/stage=(\d+)/);
    const avgMatch = text.match(/avg_vrms=([0-9.]+)/);
    const samplesMatch = text.match(/samples=(\d+)/);
    if (stageMatch && avgMatch) {
      const stage = stageMatch[1];
      const avg = parseFloat(avgMatch[1]);
      const samples = samplesMatch ? samplesMatch[1] : '?';
      recordedStages[`s${stage}`] = avg;
      const valId = `#calS${stage}Val`;
      setText(valId, formatG(avg));
      const btn = $(`#btnCalS${stage}`);
      if (btn) {
        btn.disabled = false;
        btn.classList.remove('recording');
        btn.textContent = `Start Stage ${stage}`;
      }
      const card = $(`#calCardS${stage}`);
      if (card) card.classList.remove('recording');
      if (statusBadge) {
        statusBadge.textContent = 'IDLE';
        statusBadge.classList.remove('recording');
      }
      activeCalMode = null;
      if (feedback) {
        feedback.textContent = `✓ Stage ${stage} calibrated: True mathematical mean = ${formatG(avg)} g across ${samples} windows.`;
        feedback.className = 'cal-feedback success';
      }
      if (recordedStages.s0 !== null && recordedStages.s1 !== null) {
        if (btnCommit) btnCommit.disabled = false;
        if (recordedStages.s0 >= recordedStages.s1) {
          if (feedback) {
            feedback.textContent = `⚠️ Warning: Stage 0 (${formatG(recordedStages.s0)} g) is ≥ Stage 1 (${formatG(recordedStages.s1)} g). Ensure the imbalance bolt is securely mounted for Stage 1, or re-record.`;
            feedback.className = 'cal-feedback error';
          }
        }
      }
    }
  } else if (text.includes('ack=TARE_STOP ok=1')) {
    const tareMatch = text.match(/tare_floor(?:_g)?=([0-9.]+)/);
    const samplesMatch = text.match(/samples=(\d+)/);
    if (tareMatch) {
      const tare = parseFloat(tareMatch[1]);
      const samples = samplesMatch ? samplesMatch[1] : '?';
      recordedStages.tare = tare;
      setText('#calTareVal', formatG(tare));
      setText('#calActiveTare', formatG(tare));
      const btn = $('#btnTare');
      if (btn) {
        btn.disabled = false;
        btn.classList.remove('recording');
        btn.textContent = 'Start Tare';
      }
      const card = $('#calCardTare');
      if (card) card.classList.remove('recording');
      if (statusBadge) {
        statusBadge.textContent = 'IDLE';
        statusBadge.classList.remove('recording');
      }
      activeCalMode = null;
      if (feedback) {
        feedback.textContent = `✓ Tare baseline zeroed: Noise floor mean = ${formatG(tare)} g across ${samples} windows.`;
        feedback.className = 'cal-feedback success';
      }
    }
  } else if (text.includes('ack=CAL_COMMIT ok=1')) {
    const warnMatch = text.match(/warn_enter=([0-9.]+)/);
    const alarmMatch = text.match(/alarm_enter=([0-9.]+)/);
    const tareMatch = text.match(/tare=([0-9.]+)/);
    if (warnMatch) setText('#calActiveWarn', formatG(parseFloat(warnMatch[1])));
    if (alarmMatch) setText('#calActiveAlarm', formatG(parseFloat(alarmMatch[1])));
    if (tareMatch) setText('#calActiveTare', formatG(parseFloat(tareMatch[1])));
    
    if (feedback) {
      feedback.textContent = '✓ Thresholds successfully saved to ESP32 Flash memory!';
      feedback.className = 'cal-feedback success';
    }
    if (btnCommit) {
      btnCommit.textContent = 'Saved to Flash ✓';
      btnCommit.disabled = true;
    }
  } else if (text.includes('ack=UNKNOWN') && text.includes('CAL_COMMIT')) {
    if (feedback) {
      feedback.textContent = '⚠️ ESP32 rejected CAL_COMMIT: hardware is running older firmware. Please flash the updated sketch to ESP32.';
      feedback.className = 'cal-feedback error';
    }
    if (btnCommit) {
      btnCommit.disabled = false;
      btnCommit.textContent = 'Save Thresholds to Flash';
    }
  } else if (text.includes('ack=CAL_RESET ok=1')) {
    if (feedback) {
      feedback.textContent = '✓ Factory defaults restored in Flash memory.';
      feedback.className = 'cal-feedback info';
    }
  }
}

window.addEventListener('DOMContentLoaded',()=>{charts.init();bind();store.subscribe(render);store.addEvent({type:'session_started',severity:'info',title:'Dashboard session started',detail:`${mode} source selected.`});source.subscribe(sample=>store.ingest(sample),status=>{store.setConnection({browserToBridge:status.status,bridgeToSerial:status.serial||store.state.connection.bridgeToSerial,sensor:status.sensor||store.state.connection.sensor});if(status.capabilities)store.setCapabilities(status.capabilities);},event=>handleBridgeEvent(event));source.connect();setInterval(()=>store.tick(),500);});
window.addEventListener('beforeunload',()=>source.disconnect());
