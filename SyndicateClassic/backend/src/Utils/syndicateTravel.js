import {clientSettings} from './syndicateClientSettings.js';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import http from 'node:http';
import net from 'node:net';
import {fileURLToPath} from 'node:url';
import User from '../User/Mongodb/Schema/user.js';
import Profile from '../User/Mongodb/Schema/profiles.js';
import Friends from '../User/Mongodb/Schema/friends.js';
import {withAccountLock} from './syndicateAccountLock.js';
import {digest,validateProfile,signed,verified} from './syndicateTravelCore.js';
import {guestKey,DIRECT_HEADER,NAME_HEADER,fromInternet,checkSocketLine,guestHeaders} from './syndicateDirect.js';
const root=fileURLToPath(new URL('../../../Runtime/',import.meta.url));
const dir=path.join(root,'AccountTravel');fs.mkdirSync(dir,{recursive:true});
function read(file,fallback=null){try{return JSON.parse(fs.readFileSync(file,'utf8'));}catch{return fallback;}}
function write(file,value){fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file+'.tmp',JSON.stringify(value));fs.renameSync(file+'.tmp',file);}
export function selectedHost(){const h=read(path.join(root,'Server','remote-host.json'),{})?.host||'';return /^(?:\d{1,3}\.){3}\d{1,3}$/.test(h)?h:'';}
const file=id=>path.join(dir,id+'.json');
const pendingDir=path.join(process.env.LOCALAPPDATA||root,'ProjectTilted','UserData','PendingTravel');fs.mkdirSync(pendingDir,{recursive:true});
export function travelActive(id){return /^[a-f0-9]{32}$/i.test(id||'')&&!!read(file(id));}
const keyDir=path.join(process.env.LOCALAPPDATA||root,'ProjectTilted','UserData','TravelKeys');
// Kept outside the versioned install folder, so a launcher update keeps the same identity
// (a new identity made the host refuse with "already belongs to another identity").
function keysFor(id){const p=path.join(keyDir,id+'.json');let key=read(p);if(!key){key=read(path.join(dir,'keys',id+'.json'));if(key)write(p,key);}if(!key){const pair=crypto.generateKeyPairSync('ed25519');key={publicKey:pair.publicKey.export({type:'spki',format:'pem'}),privateKey:pair.privateKey.export({type:'pkcs8',format:'pem'})};write(p,key);}return key;}
async function request(lease,action,extra={},ownerKeys=null){
 let response;try{response=await fetch(`http://${lease.host}:3552/travel`,{method:'POST',headers:{'Content-Type':'application/json',...guestHeaders(lease.username)},body:JSON.stringify(signed(ownerKeys||keysFor(lease.accountId),{channel:"public",action,accountId:lease.accountId,sessionId:lease.sessionId,...extra})),signal:AbortSignal.timeout(action==='open'?75000:20000)});}catch{throw Error('Cannot reach the previous Radmin host '+lease.host+'. Leave the remote session to continue locally; pending progress will be preserved.');}
 // The whole account profile goes over Radmin, which can be slow; 5 seconds was too short.
 let result;try{result=await response.json();}catch{throw Error('The host at '+lease.host+' took too long to answer the account transfer. Try again; if it keeps happening, check the Radmin connection (direct, not relayed).');}if(!response.ok)throw Error(result.message||'The friend host did not accept the account transfer.');return result;
}
function snapshot(doc,friends){return {profiles:doc.profiles,events:doc.syndicateProgressionEvents||[],friends:friends?.list||{accepted:[],incoming:[],outgoing:[],blocked:[]}};}
async function current(id){const doc=await Profile.collection.findOne({accountId:id});if(!doc)throw Error('Local account profile is missing.');return snapshot(doc,await Friends.collection.findOne({accountId:id}));}
function exchange(accountId){const code=crypto.randomBytes(24).toString('hex');global.exchangeCodes.push({exchange_code:code,accountId,reusable:true,expires:Date.now()+600000});setTimeout(()=>{const n=global.exchangeCodes.findIndex(e=>e.exchange_code===code);if(n>=0)global.exchangeCodes.splice(n,1);},600000).unref();return code;}
const used=new Map();
export async function handleTravel(req,res){
 try{
  if(selectedHost())throw Error('The PC at that Radmin IP is set to join someone else, so it cannot host. On the HOST PC, clear Settings > Play with friends (leave it empty) and relaunch; the guest enters the host\'s Radmin IP, not their own.');
  const body=verified(req.body),pin=digest(req.body.publicKey);
   if(body.channel!=="public")throw Error("This host only accepts public Syndicate accounts.");
  const nonce=pin+':'+body.nonce;if(used.has(nonce))throw Error('Transfer request already used.');used.set(nonce,Date.now());
  for(const [k,v] of used)if(Date.now()-v>120000)used.delete(k);
  const result=await withAccountLock(body.accountId,async()=>{
   let user=await User.collection.findOne({accountId:body.accountId});
   if(user&&!user.travelGuest)throw Error('This account ID belongs to a player who lives on this host.');
   const rejoin=user&&user.travelGuest.owner!==pin;
   if(rejoin&&(body.action!=='open'||user.username_lower!==String(body.username||'').toLowerCase()))throw Error('This account ID already belongs to another identity on this host.');
   if(rejoin)console.warn('[Syndicate travel] Visiting account',body.accountId,'reconnected with a new identity (update or reinstall); taking it over.');
   if(body.action==='open'){
    validateProfile(body.accountId,body.data?.profiles);
    if(body.clientSettings!==undefined)clientSettings.validate(body.clientSettings);
    if(!/^[a-zA-Z0-9_ -]{3,24}$/.test(body.username||''))throw Error('Invalid display name.');
    if(user?.travelGuest?.sessionId===body.sessionId&&user.travelGuest.status==='active')return {code:exchange(body.accountId),resumed:true};
    // An unfinished earlier visit (timeout, crash, update) is replaced by the new one; the guest
    // sends its own saved profile, so nothing on the guest's PC is lost.
    if(user?.travelGuest?.status==='active')console.warn('[Syndicate travel] Replacing unfinished hosted session for',body.accountId);
    if(await User.exists({username_lower:body.username.toLowerCase(),accountId:{$ne:body.accountId}}))throw Error('That display name is already used on this host.');
    const record={owner:pin,sessionId:body.sessionId,status:'loading'};
    if(!user){await User.collection.insertOne({created:new Date(),accountId:body.accountId,discordId:'travel:'+body.accountId,username:body.username,username_lower:body.username.toLowerCase(),email:body.accountId+'@guest.invalid',password:crypto.randomBytes(64).toString('hex'),banned:false,travelGuest:record});}
    else await User.collection.updateOne({accountId:body.accountId},{$set:{travelGuest:record}});
    await Profile.collection.updateOne({accountId:body.accountId},{$set:{profiles:body.data.profiles,syndicateProgressionEvents:body.data.events||[]},$setOnInsert:{created:new Date()}},{upsert:true});
    await Friends.collection.updateOne({accountId:body.accountId},{$set:{list:body.data.friends||{accepted:[],incoming:[],outgoing:[],blocked:[]}},$setOnInsert:{created:new Date()}},{upsert:true});
    if(body.clientSettings!==undefined)clientSettings.apply(body.accountId,body.clientSettings);
    await User.collection.updateOne({accountId:body.accountId},{$set:{'travelGuest.status':'active'}});
    return {code:exchange(body.accountId)};
   }
   if(!user||user.travelGuest.sessionId!==body.sessionId)throw Error('Hosted account session not found.');
   if(body.action==='close')await User.collection.updateOne({accountId:body.accountId},{$set:{'travelGuest.status':'closed'}});
   if(!['snapshot','close'].includes(body.action))throw Error('Unknown account action.');
   const data=await current(body.accountId);return {data,hash:digest(data),clientSettings:clientSettings.snapshot(body.accountId),closed:body.action==='close'||user.travelGuest.status==='closed'};
  });res.json(result);
 }catch(error){res.status(409).json({message:error.message});}
}
async function syncLocked(id,close=false){
 const lease=read(file(id));if(!lease)return;
 // Freeze the host before taking the final snapshot. Retrying close is idempotent.
 const result=await request(lease,close?'close':'snapshot');validateProfile(id,result.data?.profiles);
 if(result.clientSettings!==undefined)clientSettings.validate(result.clientSettings);
 if(digest(result.data)!==result.hash)throw Error('Transfer snapshot failed verification.');
 const local=await current(id),hash=digest(local);
 // Recover a crash between applying the snapshot and recording the applied hash.
 if(hash!==lease.appliedHash&&hash!==lease.pendingHash)throw Error('Local profile changed during hosted play. Both copies were preserved for recovery.');
 lease.pendingHash=result.hash;write(file(id),lease);
 await Profile.collection.updateOne({accountId:id},{$set:{profiles:result.data.profiles,syndicateProgressionEvents:result.data.events||[]}});
 await Friends.collection.updateOne({accountId:id},{$set:{list:result.data.friends}},{upsert:true});
 if(result.clientSettings!==undefined)clientSettings.apply(id,result.clientSettings);
 lease.appliedHash=result.hash;delete lease.pendingHash;write(file(id),lease);
 if(close||result.closed){fs.unlinkSync(file(id));}
}
async function deferLocked(id,error){
 const lease=read(file(id));if(!lease)return {pending:false};
 const record={lease,keys:keysFor(id),local:await current(id),clientSettings:clientSettings.snapshot(id),reason:error.message,savedAt:new Date().toISOString()};
 // Write and verify the durable recovery record before releasing the active lease.
 const target=path.join(pendingDir,id+'-'+lease.sessionId+'.json');write(target,record);
 if(digest(read(target))!==digest(record))throw Error('Could not verify the pending account backup.');
 fs.unlinkSync(file(id));return {pending:true,message:'Disconnected. Playing from the last local save; remote progress is pending recovery when the previous host is reachable.'};
}
async function recoverPending(id,host){
 for(const name of fs.readdirSync(pendingDir).filter(n=>n.startsWith(id+'-')&&n.endsWith('.json'))){
  const source=path.join(pendingDir,name),record=read(source);if(!record?.lease || record.lease.host!==host)continue;
  const result=await request(record.lease,'close',{},record.keys);validateProfile(id,result.data?.profiles);
  if(digest(result.data)!==result.hash)throw Error('Pending travel snapshot failed verification.');
  if(result.clientSettings!==undefined)clientSettings.validate(result.clientSettings);
  const local=await current(id);const unchanged=[record.lease.appliedHash,record.lease.pendingHash].includes(digest(local));
  const recovered={...record,remote:result,localBeforeRecovery:local,conflict:!unchanged};
  const archive=path.join(pendingDir,'recovered',name);write(archive,recovered);
  if(digest(read(archive))!==digest(recovered))throw Error('Could not verify the recovered progress backup.');
  if(unchanged){await Profile.collection.updateOne({accountId:id},{$set:{profiles:result.data.profiles,syndicateProgressionEvents:result.data.events||[]}});await Friends.collection.updateOne({accountId:id},{$set:{list:result.data.friends}},{upsert:true});if(result.clientSettings!==undefined)clientSettings.apply(id,result.clientSettings);}
  else console.warn('[Syndicate travel] Both local and remote progress preserved for recovery:',archive);
  fs.unlinkSync(source);
 }
}
export async function finishTravel(id){return withAccountLock(id,async()=>{try{await syncLocked(id,true);return {pending:false};}catch(error){return deferLocked(id,error);}});}
export async function openTravel(id){return withAccountLock(id,async()=>{
 const host=selectedHost();let lease=read(file(id));
 if(lease&&lease.host!==host){try{await syncLocked(id,true);}catch(error){await deferLocked(id,error);}lease=null;}
 if(!host)return null;
 await recoverPending(id,host);
 if(lease){try{await syncLocked(id);lease=read(file(id));}catch(error){if(!error.message.includes('Hosted account session not found'))throw error;}}
 const user=await User.findOne({accountId:id}).lean();if(!user)throw Error('Sign in locally first.');
 if(user.travelGuest)throw Error('A hosted guest account cannot be exported again.');
 if(!lease){const data=await current(id);validateProfile(id,data.profiles);lease={accountId:id,host,sessionId:crypto.randomBytes(16).toString('hex'),appliedHash:digest(data)};
  write(path.join(dir,'backups',id+'-'+Date.now()+'.json'),{...data,clientSettings:clientSettings.snapshot(id)});write(file(id),lease);}
 const data=await current(id);return request(lease,'open',{username:user.username,data,clientSettings:clientSettings.snapshot(id)});
});}
let syncing=false;
// Syndicate fix: a lease for a host that is no longer selected (you went back to playing
// locally) used to be retried forever, so the account stayed blocked with
// "This account is playing on another host." Finish it now: pull the final progress
// from that host if it answers, otherwise save both copies to PendingTravel (recovered
// automatically if you reconnect to that host) and release the account.
async function travelTick(){if(syncing)return;syncing=true;try{for(const name of fs.readdirSync(dir).filter(n=>/^[a-f0-9]{32}\.json$/.test(n))){const id=name.slice(0,-5);try{await withAccountLock(id,async()=>{const lease=read(file(id));if(!lease)return;if(lease.host!==selectedHost()){try{await syncLocked(id,true);}catch(error){const outcome=await deferLocked(id,error);console.warn('[Syndicate travel] Released stale hosted session for',id,'-',outcome.message||'synced');}}else await syncLocked(id);});}catch{/* Keep the lease and retry; never discard unsynced progress. */}}}finally{syncing=false;}}
setInterval(travelTick,5000).unref();
setTimeout(travelTick,1500).unref();
export function proxyLobby(req,res,next){
 const host=selectedHost();if(!host||req.path.startsWith('/launcher'))return next();
 const headers={...req.headers,host:`${host}:3551`};delete headers.connection;delete headers[DIRECT_HEADER];delete headers[NAME_HEADER];Object.assign(headers,guestHeaders(''));
 const upstream=http.request({hostname:host,port:3551,path:req.originalUrl,method:req.method,headers,timeout:20000},reply=>{res.writeHead(reply.statusCode,reply.headers);reply.pipe(res);});
 upstream.on('timeout',()=>upstream.destroy());upstream.on('error',()=>{if(!res.headersSent)res.status(503).json({message:'Friend lobby is unavailable.'});else res.destroy();});req.on('aborted',()=>upstream.destroy());req.pipe(upstream);
}
export function startSocialRelay(){
 const pipe=(client,remote,first)=>{const close=()=>{client.destroy();remote.destroy();};client.on('error',close);remote.on('error',close);client.on('close',()=>remote.destroy());remote.on('close',()=>client.destroy());
  if(first&&first.length)remote.write(first);client.pipe(remote);remote.pipe(client);};
 const server=net.createServer(client=>{
  const host=selectedHost();
  if(host){ // joining someone: forward to the host (a share-code join starts with the join key)
   const remote=net.connect(70,host);const key=guestKey();if(key)remote.write('SYNDICATE '+key+'\n');pipe(client,remote);return;}
  // Hosting. A share-code guest's connection starts with "SYNDICATE <key>\n" (required from the internet; on a home
  // network or Radmin it is accepted and stripped); every other connection is passed through as it comes.
  const internet=fromInternet(client.remoteAddress);
  let buffer=Buffer.alloc(0);const timer=setTimeout(()=>client.destroy(),5000);
  const onData=chunk=>{buffer=Buffer.concat([buffer,chunk]);
   const tagged=buffer.length>=7?buffer.subarray(0,7).toString('latin1')==='SYNDICATE ':'SYNDICATE '.startsWith(buffer.toString('latin1'));
   if(!tagged){clearTimeout(timer);client.off('data',onData);if(internet){client.destroy();return;}client.pause();pipe(client,net.connect(7070,'127.0.0.1'),buffer);client.resume();return;}
   const at=buffer.indexOf(10);
   if(at<0){if(buffer.length>200){clearTimeout(timer);client.destroy();}return;}
   client.off('data',onData);clearTimeout(timer);client.pause();
   if(!checkSocketLine(client.remoteAddress,buffer.subarray(0,at).toString('utf8'))){client.destroy();return;}
   pipe(client,net.connect(7070,'127.0.0.1'),buffer.subarray(at+1));client.resume();};
  client.on('data',onData);client.on('error',()=>{clearTimeout(timer);client.destroy();});
 });
 server.on('error',e=>console.error('[Syndicate lobby relay]',e.code));server.listen(70);
}
