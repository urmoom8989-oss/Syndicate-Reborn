const fs=require('fs'),path=require('path'),crypto=require('crypto');
const runtime=path.resolve(__dirname,'../../Runtime/Server');
// Fortnite 4.00 sends PlaylistId numbers; newer clients send asset names.
const modes={'2':'solo','9':'squad','10':'duo',playlist_defaultsolo:'solo',
 // Chapter 2 entrance (11.00 Apollo intro): the game's own intro playlists start the intro match (mode 'intro').
 playlist_intro_apollo_newplayer:'intro',playlist_intro_apollo_pastplayer:'intro',playlist_intro_apollo_currentplayer:'intro',playlist_intro_apollo_05h95b:'intro',playlist_intro_apollo_20h80b:'intro',playlist_intro_apollo_30h70b:'intro',playlist_intro_apollo_40h60b:'intro',
 playlist_defaultduo:'duo',playlist_defaultsquad:'squad',playlist_50v50:'50v50',playlist_playground:'playground',playlist_showdownalt_solo:'arenasolo',playlist_playgroundv2:'creative'};
const playlistNames={intro:'Playlist_Intro_Apollo_NewPlayer',solo:'Playlist_DefaultSolo',duo:'Playlist_DefaultDuo',squad:'Playlist_DefaultSquad','50v50':'Playlist_50v50',playground:'Playlist_Playground',arenasolo:'Playlist_ShowdownAlt_Solo',creative:'Playlist_PlaygroundV2'};
const read=(file)=>{try{return JSON.parse(fs.readFileSync(file,'utf8'));}catch{return null;}};
function write(file,value){fs.mkdirSync(path.dirname(file),{recursive:true});const temp=file+'.'+crypto.randomUUID()+'.tmp';fs.writeFileSync(temp,JSON.stringify(value));fs.renameSync(temp,file);}
function playlistMode(bucket){const parts=typeof bucket==='string'?bucket.split(':'):[];if(parts.length!==4||!modes[parts[3].toLowerCase()])throw Error('Choose Solo, Duos, Squads, Arena or Creative in Fortnite. This playlist is not supported.');return modes[parts[3].toLowerCase()];}
function selectedBuild(dir=runtime){return read(path.join(dir,'active-build.json'))?.Build || '4.5';}
function requireBuild(build,dir=runtime){const allowed=['1.7.2','1.8.2','1.8','1.11','2.5','3.5.1','4.5','5.10','6.20','7.40','8.50','9.40','10.31','10.40','11.00','12.50'];if(process.env.TILTED_DEVKIT==='1')allowed.push('19.00');if(!allowed.includes(build)||build!==selectedBuild(dir))throw Error('Host and player must select the same Fortnite version.');return build;}
function hostStatus(dir=runtime,now=Date.now()){
 try {
 const state=read(path.join(dir,'server.json')),live=read(path.join(dir,'occupancy.json'));
 if(!state||!live||state.Pid!==live.Pid)return null;
 const modified=fs.statSync(path.join(dir,'occupancy.json')).mtimeMs;
 const started=Number(state.Started)/10000-62135596800000;
 if(modified+1<started||now-modified>4000||!Number.isInteger(live.Connected)||live.Connected<0)return null;
 try{process.kill(state.Pid,0);}catch{return null;}
 return {...live,Mode:state.Mode,Build:state.Build||'4.5'};
 }catch{return null;}
}
// Fast queue: the launcher can run the live match server on 7778 while the next one loads on 7777 (or the other
// way round). server.json says which port the live server has; anything else means the usual 7777.
function hostPort(dir=runtime){const p=Number(read(path.join(dir,'server.json'))?.Port);return Number.isInteger(p)&&p>=7777&&p<=7799?p:null;}
function ticketPath(id,dir=runtime){if(!/^[a-f0-9]{32}$/.test(id||''))throw Error('Invalid matchmaking ticket');return path.join(dir,'playlist-tickets',id+'.json');}
function readTicket(id,dir=runtime){try{const t=read(ticketPath(id,dir));return t&&t.expires>Date.now()?t:null;}catch{return null;}}
function reserve(mode,accountId,dir=runtime,now=Date.now(),build=selectedBuild(dir)){
 requireBuild(build,dir);
 if(!playlistNames[mode])throw Error('Unsupported playlist');
 if(mode==='50v50' && build!=='4.5')throw Error('50v50 is available only in Season 4.');
 if(mode==='playground' && build!=='4.5')throw Error('Playground is available only in Season 4.');
 if(mode==='intro' && build!=='11.00')throw Error('The Chapter 2 entrance needs Fortnite 11.00.');
 if(mode.startsWith('arena') && !['8.50','9.40','10.31','10.40','11.00','12.50'].includes(build))throw Error('Arena is available in Season 8 and later.');
 // Creative is off by default: the island never loads on UE4 (LoadPlayset faults; see project doc
 // creative-7.40-2026-10-27). Dev Kit may select it on 11.00 to experiment - expect empty sea / no island
 // until the playset-streaming blocker is solved.
 if(mode==='creative' && !(process.env.TILTED_DEVKIT==='1' && build==='11.00'))throw Error('Creative is turned off for now.');
 const live=hostStatus(dir,now),requestFile=path.join(dir,'playlist-request.json');
 let request=read(requestFile);const ticketsDir=path.join(dir,'playlist-tickets');fs.mkdirSync(ticketsDir,{recursive:true});
 let pending=false;
 // Fortnite sends two ticket requests at once when matchmaking starts (one it then cancels).
 // Both must get the same ticket: otherwise the later request cancels the ticket the game
 // actually waits on and the player stays "Queued" forever.
 for(const file of fs.readdirSync(ticketsDir).filter(f=>/^[a-f0-9]{32}\.json$/.test(f))){const t=read(path.join(ticketsDir,file));
  if(t&&t.accountId===accountId&&!t.assignedPid&&!t.host&&t.mode===mode&&(t.build||'4.5')===build&&t.reservedUntil>now&&t.expires>now&&now-(t.expires-900000)<15000&&t.requestId===request?.Id)return {...t,reused:true};}
 for(const file of fs.readdirSync(ticketsDir).filter(f=>/^[a-f0-9]{32}\.json$/.test(f))){const t=read(path.join(ticketsDir,file));if(!t||t.expires<=now){fs.unlinkSync(path.join(ticketsDir,file));continue;}if(t.accountId===accountId && !t.assignedPid && t.reservedUntil>now){t.reservedUntil=0;write(path.join(ticketsDir,file),t);continue;}// Only someone else still waiting to join counts as "being prepared": a ticket that already joined its match
  // (assignedPid; kept reserved 90 s so party members can follow) or the asking player's own ticket must not
  // block a new playlist. Before, dying within 90 s of joining Solo made Duos/Squads fail with this error.
  if(t.requestId===request?.Id&&t.reservedUntil>now&&(t.build||'4.5')===build&&!t.assignedPid&&t.accountId!==accountId)pending=true;}
 if(live&&live.Connected>0&&(live.Mode!==mode||live.Build!==build||!live.Accepting))throw Error('This host already has a match with players. Wait for it to finish before starting another playlist.');
 if(live&&live.Mode===mode&&!live.Accepting&&live.Connected>0)throw Error('This match has already started.');
 if(request&&(request.Mode!==mode||(request.Build||'4.5')!==build)&&pending)throw Error('Another playlist is being prepared for players. Cancel that search or wait for their match.');
 if(!request||request.Mode!==mode||(request.Build||'4.5')!==build||request.Expires<=now||!pending||read(path.join(dir,'playlist-error.json'))?.Id===request.Id){request={Id:crypto.randomUUID(),Mode:mode,Build:build,Expires:now+600000};}
 else request.Expires=now+600000;
 write(requestFile,request);
 const id=crypto.randomBytes(16).toString('hex');const t={id,mode,build,accountId,requestId:request.Id,expires:now+900000,reservedUntil:now+600000};write(ticketPath(id,dir),t);return t;
}
// Lobby pre-switch. A match server runs one playlist, so picking Duos or Squads used to restart it only when the
// player pressed Ready, and they waited the whole server start in the queue. The lobby tells the backend the
// captain's playlist as soon as it is picked (party meta, Routes/party.js); this asks the launcher for that
// playlist right then, so the server is ready (or nearly) by the time Ready is pressed. It never touches a
// server with players in it or a playlist someone else is waiting for, and reserve() works exactly as before.
function prewarm(mode,dir=runtime,now=Date.now()){
 if(!playlistNames[mode]||mode==='intro'||mode==='creative')return false;
 const build=selectedBuild(dir);
 if(['50v50','playground'].includes(mode)&&build!=='4.5')return false;
 if(mode.startsWith('arena')&&!['8.50','9.40','10.31','10.40','11.00','12.50'].includes(build))return false;
 const live=hostStatus(dir,now);
 if(live&&live.Connected>0)return false;
 if(live&&live.Mode===mode&&live.Build===build)return false;
 // A server for this playlist is already starting (no occupancy yet).
 const state=read(path.join(dir,'server.json'));
 if(!live&&state&&state.Mode===mode&&(state.Build||'4.5')===build){try{process.kill(state.Pid,0);return false;}catch{}}
 const requestFile=path.join(dir,'playlist-request.json'),request=read(requestFile);
 if(request&&request.Mode===mode&&(request.Build||'4.5')===build&&request.Expires>now)return false;
 const ticketsDir=path.join(dir,'playlist-tickets');
 try{for(const file of fs.readdirSync(ticketsDir).filter(f=>/^[a-f0-9]{32}\.json$/.test(f))){const t=read(path.join(ticketsDir,file));
  if(t&&t.expires>now&&t.reservedUntil>now&&!t.assignedPid&&!t.host)return false;}}catch{}
 write(requestFile,{Id:crypto.randomUUID(),Mode:mode,Build:build,Expires:now+600000,Prewarm:true});
 return true;
}
function modeFromName(name){const m=typeof name==='string'?name.match(/Playlist_[A-Za-z0-9_]+/):null;return m?modes[m[0].toLowerCase()]||null:null;}
function ready(id,dir=runtime){const t=readTicket(id,dir),r=read(path.join(dir,'playlist-request.json'));if(!t||t.reservedUntil<=Date.now()||t.requestId!==r?.Id)return false;const live=hostStatus(dir);return !!live&&live.Mode===t.mode&&live.Build===(t.build||'4.5')&&selectedBuild(dir)===(t.build||'4.5')&&(!t.assignedPid||t.assignedPid===live.Pid)&&live.Accepting&&require('./readiness.cjs').isReady(dir);}
function reserveRemote(mode,accountId,host,remoteTicket,dir=runtime,build=selectedBuild(dir)){
 if(mode==='50v50' && build!=='4.5')throw Error('50v50 is available only in Season 4.');
 if(!playlistNames[mode]||!/^(?:\d{1,3}\.){3}\d{1,3}$/.test(host)||!/^[a-f0-9]{32}$/.test(remoteTicket))throw Error('Invalid remote playlist response');
 const id=remoteTicket,t={id,mode,build,accountId,host,remoteTicket,expires:Date.now()+600000,reservedUntil:Date.now()+300000};write(ticketPath(id,dir),t);return t;
}
async function ticketReady(id,dir=runtime){const t=readTicket(id,dir);if(!t)return false;if(!t.host)return ready(id,dir);try{const r=await fetch('http://'+t.host+':3552/playlist/status/'+t.remoteTicket,{signal:AbortSignal.timeout(3000)});const s=await r.json();return r.ok&&s.ready&&s.mode===t.mode&&(s.build||'4.5')===(t.build||'4.5');}catch{return false;}}
async function releaseTicket(id,played,dir=runtime){const t=readTicket(id,dir);if(t?.host)try{await fetch('http://'+t.host+':3552/playlist/release/'+t.remoteTicket,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({played}),signal:AbortSignal.timeout(3000)});}catch{}release(id,played,dir);}
function release(id,played,dir=runtime){const t=readTicket(id,dir);if(!t)return;t.reservedUntil=played?Date.now()+90000:0;if(played)t.assignedPid=hostStatus(dir)?.Pid;write(ticketPath(id,dir),t);}
function ticketError(id,dir=runtime){const t=readTicket(id,dir),e=read(path.join(dir,'playlist-error.json'));return t && e?.Id===t.requestId?e.Message:null;}
module.exports={hostPort,prewarm,modeFromName,selectedBuild,requireBuild,ticketError,reserveRemote,ticketReady,releaseTicket,runtime,read,write,playlistMode,playlistNames,hostStatus,reserve,readTicket,ticketPath,ready,release};
