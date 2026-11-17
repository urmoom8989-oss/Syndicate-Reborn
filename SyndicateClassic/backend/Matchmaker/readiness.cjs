const fs = require('fs');
const path = require('path');
function isReady(runtime=path.resolve(__dirname,'../../Runtime/Server'), alive=pid=>{try{process.kill(pid,0);return true;}catch{return false;}}){
  try {
    const state=JSON.parse(fs.readFileSync(path.join(runtime,'server.json'),'utf8'));
    // Fast queue: a promoted background host keeps its own ready file (server.json names it).
    const name=typeof state.Ready==='string'&&/^[A-Za-z0-9_-]+\.pid$/.test(state.Ready)?state.Ready:'ready.pid';
    const ready=path.join(runtime,name);
    const started=(Number(state.Started)/10000)-62135596800000;
    if(!Number.isInteger(state.Pid)||state.Pid<=0||!Number.isFinite(started))return false;
    if(fs.readFileSync(ready,'utf8').trim()!==String(state.Pid)||fs.statSync(ready).mtimeMs+1<started||!alive(state.Pid))return false;
    const request=path.join(runtime,'restart.request');
    if(fs.existsSync(request)){
      const parts=fs.readFileSync(request,'utf8').trim().split(' ');
      if(parts[0]===String(state.Pid)&&fs.statSync(request).mtimeMs+1>=started)return false;
    }
    return true;
  }catch{return false;}
}
async function matchReady(){
  const runtime=path.resolve(__dirname,'../../Runtime/Server');
  let host='';try{host=JSON.parse(fs.readFileSync(path.join(runtime,'remote-host.json'),'utf8')).host||'';}catch{}
  if(!host)return isReady();
  if(!/^(?:\d{1,3}\.){3}\d{1,3}$/.test(host))return false;
  try{const response=await fetch(`http://${host}:3552/status`,{signal:AbortSignal.timeout(3000)});const status=await response.json();return response.ok&&status.protocol>=1&&status.ready===true;}catch{return false;}
}
module.exports={isReady,matchReady};
