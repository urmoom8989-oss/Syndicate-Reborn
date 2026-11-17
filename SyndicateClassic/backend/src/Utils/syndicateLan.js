import {handleTravel} from './syndicateTravel.js';
import {directGuard,guestKey,DIRECT_HEADER,guestHeaders} from './syndicateDirect.js';
// Private testing: accounts stay local; only match data crosses the LAN.
import express from 'express';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url);
const playlists=require('../../Matchmaker/playlists.cjs');
const teamFill=require('../../Matchmaker/fill.cjs');
const {isReady}=require('../../Matchmaker/readiness.cjs');
const runtime=fileURLToPath(new URL('../../../Runtime/Server/',import.meta.url));
const lan=path.join(runtime,'lan');
fs.mkdirSync(lan,{recursive:true});
function read(file,fallback){try{return JSON.parse(fs.readFileSync(file,'utf8'));}catch{return fallback;}}
function write(file,value){fs.writeFileSync(file+'.tmp',JSON.stringify(value));fs.renameSync(file+'.tmp',file);}
export function remoteHost(){const h=read(path.join(runtime,'remote-host.json'),{}).host||'';return /^(?:\d{1,3}\.){3}\d{1,3}$/.test(h)?h:'';}
const peersFile=path.join(lan,'peers.json'),leasesFile=path.join(lan,'leases.json');
const peers=read(peersFile,{}),leases=read(leasesFile,{});
export function validateGuest(body){
 if(!body || !/^[a-zA-Z0-9_ .-]{3,32}$/.test(body.username)|| !/^[a-f0-9]{64}$/.test(body.key))throw Error('Invalid player registration');
 const loadout={};
 for(const slot of ['character','backpack','pickaxe','glider']){
  const id=body.loadout?.[slot]||'';
  if(typeof id!=='string'|| !/^[a-zA-Z0-9_]{0,160}$/.test(id))throw Error('Invalid cosmetic');
  loadout[slot]=id;
 }
 for(const [key,value] of Object.entries(body.loadout||{})){
  // Wraps travel with a LAN/Radmin guest too (wrap_0 ... wrap_6).
  if(/^wrap_[0-7]$/.test(key)){if(typeof value==='string' && /^[a-zA-Z0-9_]{1,80}$/.test(value))loadout[key]=value;continue;}
  if(!key.startsWith('variant_'))continue;
  if(!/^variant_[a-z0-9_]{1,48}$/i.test(key)||typeof value!=='string'||value.length>48||!(/^[a-z0-9_]+(?:\.[a-z0-9_]+)*$/i.test(value)))throw Error('Invalid cosmetic style');
  if(Object.keys(loadout).filter(k=>k.startsWith('variant_')).length>=8)throw Error('Too many cosmetic styles');
  loadout[key]=value;
 }
 return {username:body.username.toLowerCase(),key:body.key,loadout};
}
async function request(host,route,body){
 const response=await fetch(`http://${host}:3552${route}`,{method:body?'POST':'GET',headers:{'Content-Type':'application/json',...guestHeaders(body?.username)},body:body?JSON.stringify(body):undefined,signal:AbortSignal.timeout(5000)});
 if(!response.ok)throw Error((await response.text()).slice(0,200));
 return response.json();
}
export async function prepareRemote(accountId,username,loadout,mode,build,fill=false){
 const host=remoteHost();if(!host)return;
 const id=crypto.createHash('sha256').update(host+'|'+accountId).digest('hex');
 const lease=leases[id]||{host,accountId,username,key:crypto.randomBytes(32).toString('hex')};
 await request(host,'/register',{username,key:lease.key,loadout});
 leases[id]=lease;write(leasesFile,leases);
 const capabilities=await request(host,'/status');
 if((capabilities.build||'4.5')!==build)throw Error('Your Radmin host is running a different Fortnite version. Select the same version on both PCs.');
 if(!capabilities.playlistSelection)throw Error('The host needs the in-game playlist launcher update.');
 return request(host,'/playlist',{username,key:lease.key,mode,build,fill});
}
export async function pullRemoteRewards(award){
 for(const lease of Object.values(leases)){
  try{
   const events=await request(lease.host,'/events',{username:lease.username,key:lease.key});
   for(const event of events){
    if(!/^\{[0-9a-f-]{36}\}$/i.test(event.id)||!['kill','win','survival','match','quest','quest_delta','xp'].includes(event.kind))continue;
    await award(lease.accountId,event);
    await request(lease.host,'/ack',{username:lease.username,key:lease.key,id:event.id});
   }
  }catch{/* Offline hosts retain rewards for the next connection. */}
 }
}
export function routeGuestReward(event){
 const peer=peers[event.username.toLowerCase()];if(!peer)return false;
 const dir=path.join(lan,'rewards',crypto.createHash('sha256').update(peer.key).digest('hex'));fs.mkdirSync(dir,{recursive:true});
 const file=path.join(dir,event.id+'.json');if(!fs.existsSync(file))write(file,event);
 return true;
}
export function startLanBridge(localUsernameExists){
 const app=express();app.use(directGuard);app.use(express.json({limit:'32mb'}));
 app.post('/travel',handleTravel);
 app.get('/status',(_,res)=>{const state=read(path.join(runtime,'server.json'),{});res.json({protocol:4,channel:"public",build:playlists.selectedBuild(),playlistSelection:true,accountTransfer:true,ready:!remoteHost()&&isReady(),mode:['solo','duo','squad','50v50'].includes(state.Mode)?state.Mode:'solo'});});
 app.post('/register',async(req,res)=>{
  try{
   if(remoteHost())return res.status(409).send('This player is joining another host.');
   const p=validateGuest(req.body),old=peers[p.username];
   if(await localUsernameExists(p.username)||old&&old.key!==p.key)return res.status(409).send('That name is already used on this host. Use a different local username.');
   if(!old&&Object.keys(peers).length>=100)return res.status(409).send('Host guest limit reached.');
   const dir=path.join(runtime,'locker');fs.mkdirSync(dir,{recursive:true});const name=Buffer.from(p.username).toString('hex');
   const file=path.join(dir,name+'.loadout');fs.writeFileSync(file+'.tmp',Object.entries(p.loadout).map(([k,v])=>`${k}=${v}`).join('\n'));fs.renameSync(file+'.tmp',file);
   if(p.loadout.character)fs.writeFileSync(path.join(dir,name+'.txt'),p.loadout.character);
   peers[p.username]={key:p.key};write(peersFile,peers);res.json({ok:true});
  }catch(error){res.status(400).send(error.message);}
 });
 function authenticated(req,res,next){
  const name=typeof req.body?.username==='string'?req.body.username.toLowerCase():'';
  const peer=Object.hasOwn(peers,name)?peers[name]:null;
  if(!peer||req.body.key!==peer.key)return res.status(403).end();
  req.rewardDir=path.join(lan,'rewards',crypto.createHash('sha256').update(peer.key).digest('hex'));next();
 }
 app.post('/playlist',authenticated,(req,res)=>{try{if(remoteHost())throw Error('This host is joining another host.');const t=playlists.reserve(req.body.mode,'lan:'+req.body.username.toLowerCase(),undefined,Date.now(),req.body.build||'4.5');t.fill=teamFill.parseFill(req.body.fill);t.username=req.body.username.toLowerCase();playlists.write(playlists.ticketPath(t.id),t);res.json({ticket:t.id,mode:t.mode,build:t.build});}catch(e){res.status(409).json({error:e.message});}});
 app.get('/playlist/status/:id',(req,res)=>{const t=playlists.readTicket(req.params.id);const ready=!!t&&playlists.ready(t.id);if(ready&&t.username){const live=playlists.hostStatus();if(live)teamFill.writeSelection(runtime,t.username,t.fill===true,t.mode,live.Pid);}res.json({ready,mode:t?.mode,build:t?.build});});
 app.post('/playlist/release/:id',(req,res)=>{playlists.release(req.params.id,req.body?.played===true);res.json({ok:true});});
 app.post('/events',authenticated,(req,res)=>{const names=fs.existsSync(req.rewardDir)?fs.readdirSync(req.rewardDir):[];res.json(names.filter(n=>/^\{[0-9a-f-]{36}\}\.json$/i.test(n)).slice(0,100).map(n=>read(path.join(req.rewardDir,n),null)).filter(Boolean));});
 app.post('/ack',authenticated,(req,res)=>{if(!/^\{[0-9a-f-]{36}\}$/i.test(req.body.id))return res.status(400).end();fs.rmSync(path.join(req.rewardDir,req.body.id+'.json'),{force:true});res.json({ok:true});});
 const server=app.listen(3552,'0.0.0.0');server.on('error',e=>console.error('[Syndicate LAN] bridge unavailable:',e.code));return server;
}
