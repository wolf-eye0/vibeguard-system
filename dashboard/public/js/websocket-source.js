import { normalizePacket } from './telemetry-adapter.js';

export class WebSocketSource {
  constructor(url){this.url=url;this.socket=null;this.messageHandler=()=>{};this.statusHandler=()=>{};this.eventHandler=()=>{};this.attempt=0;this.closed=false;this.pending=new Map();}
  subscribe(onMessage,onStatus,onEvent){this.messageHandler=onMessage;this.statusHandler=onStatus;if(onEvent)this.eventHandler=onEvent;}
  connect(){this.closed=false;this.open();}
  open(){
    this.statusHandler({status:'CONNECTING',attempt:this.attempt});
    this.socket=new WebSocket(this.url);
    this.socket.addEventListener('open',()=>{this.attempt=0;this.statusHandler({status:'CONNECTED'});});
    this.socket.addEventListener('message',event=>{
      try{const message=JSON.parse(event.data);
        if(message.type==='telemetry'){const sample=normalizePacket(message.packet,'LIVE',message.bridgeTsMs);this.messageHandler(sample);}
        if(message.type==='hello'||message.type==='bridge_status')this.statusHandler({status:'CONNECTED',serial:message.connection?.status,sensor:message.connection?.sensor,capabilities:message.capabilities});
        if(message.type==='bridge_event')this.eventHandler(message);
        if(message.type==='command_result'){const pending=this.pending.get(message.requestId);if(pending){this.pending.delete(message.requestId);message.ok?pending.resolve(message):pending.reject(new Error(message.error));}}
      }catch(error){this.statusHandler({status:'FAULT',error:error.message});}
    });
    this.socket.addEventListener('error',()=>this.statusHandler({status:'FAULT',error:'WebSocket connection error'}));
    this.socket.addEventListener('close',()=>{this.statusHandler({status:'DISCONNECTED'});if(!this.closed){const wait=Math.min(1000*2**this.attempt++,10000);setTimeout(()=>this.open(),wait);}});
  }
  disconnect(){this.closed=true;this.socket?.close();}
  sendCommand(command,payload={}){
    return new Promise((resolve,reject)=>{if(this.socket?.readyState!==WebSocket.OPEN)return reject(new Error('Bridge is not connected.'));const requestId=crypto.randomUUID();this.pending.set(requestId,{resolve,reject});this.socket.send(JSON.stringify({type:'command',requestId,command,payload}));setTimeout(()=>{if(this.pending.delete(requestId))reject(new Error('Command timed out.'));},5000);});
  }
}
