// Direct play (no Radmin): the host's launcher opens Syndicate's ports on the router with UPnP and gives the host a
// share code (the host's public IP plus a secret join key). Friends paste the code; their launcher saves the host
// and the key (Runtime/Server/remote-host.json) and every request their backend forwards to the host carries the key.
//
// Anything that reaches this PC from the internet (not loopback, home network, Radmin or Hamachi) is refused unless
// direct play is switched on (Runtime/Server/direct-host.json) and the request carries that key. Radmin, Hamachi
// and home-network friends keep working exactly as before.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const runtime=fileURLToPath(new URL('../../../Runtime/Server/',import.meta.url));
export const DIRECT_HEADER='x-tilted-direct';
function read(file){try{return JSON.parse(fs.readFileSync(path.join(runtime,file),'utf8'));}catch{return {};}}
let hostCache={at:0,value:null};
function hostSettings(){const now=Date.now();if(now-hostCache.at>2000){hostCache={at:now,value:read('direct-host.json')};}return hostCache.value||{};}
// The key this PC sends when it joins a host by share code ('' for Radmin / LAN joins).
export function guestKey(){const k=read('remote-host.json').key;return typeof k==='string' && /^[a-f0-9]{32}$/.test(k)?k:'';}
// A code stops working when it expires (expires = ms since 1970, written by the launcher; 6 hours after opening).
function hostKey(){const s=hostSettings();if(typeof s.expires==='number' && Date.now()>s.expires)return '';return s.on===true && typeof s.key==='string' && /^[a-f0-9]{32}$/.test(s.key)?s.key:'';}
// Join approval: a friend with a valid code still waits until the host presses Allow in the launcher.
// direct-approved.json (written by the host's launcher): {key, allowed:{ip:name}, denied:{ip:name}}; lists from an older
// code do not carry over. direct-pending.json (written here): {ip:{name,at}} for the launcher to ask about.
let approvedCache={at:0,value:{}};
function approvals(){const now=Date.now();if(now-approvedCache.at>1000){approvedCache={at:now,value:read('direct-approved.json')};}const a=approvedCache.value||{};return a.key===hostKey()?a:{};}
function plainIp(address){let a=String(address||'');return a.startsWith('::ffff:')?a.slice(7):a;}
const pending={};let pendingWritten=0;
function askHost(ip,name){
 const now=Date.now();const known=pending[ip];
 pending[ip]={name:String(name||known?.name||'').replace(/[^\w .~-]/g,'').slice(0,32),at:known?.at||now,last:now};
 for(const [k,v] of Object.entries(pending))if(now-v.last>120000)delete pending[k]; // stopped asking
 if(now-pendingWritten<1000 && known)return;pendingWritten=now;
 try{fs.writeFileSync(path.join(runtime,'direct-pending.json'),JSON.stringify(pending));}catch{}
}
// Last time a let-in friend talked to this PC (the launcher closes the ports after 30 minutes without any).
let activityWritten=0;
function touch(){const now=Date.now();if(now-activityWritten<30000)return;activityWritten=now;try{fs.writeFileSync(path.join(runtime,'direct-activity.json'),JSON.stringify({at:now}));}catch{}}
// 'ok', 'wait' (valid code, host has not answered), 'denied' or 'bad' (no or wrong code).
function admit(address,given,name){
 if(!keyMatches(given))return 'bad';
 const ip=plainIp(address);const a=approvals();
 if(a.denied && Object.prototype.hasOwnProperty.call(a.denied,ip))return 'denied';
 if(a.allowed && Object.prototype.hasOwnProperty.call(a.allowed,ip)){touch();return 'ok';}
 askHost(ip,name);return 'wait';
}
export const NAME_HEADER='x-tilted-name';
// Loopback, home networks, carrier NAT, Radmin (26.x) and Hamachi (25.x) count as "not the internet".
export function fromInternet(address){
 let a=String(address||'');if(a.startsWith('::ffff:'))a=a.slice(7);
 if(a==='::1'||a==='')return false;
 const m=/^(\d+)\.(\d+)\.(\d+)\.(\d+)$/.exec(a);if(!m)return true; // other IPv6: treat as internet
 const [b0,b1]=[Number(m[1]),Number(m[2])];
 if(b0===127||b0===10||b0===25||b0===26)return false;
 if(b0===192&&b1===168)return false;
 if(b0===172&&b1>=16&&b1<=31)return false;
 if(b0===169&&b1===254)return false;
 if(b0===100&&b1>=64&&b1<=127)return false;
 return true;
}
function keyMatches(given){
 const key=hostKey();if(!key||typeof given!=='string'||given.length!==key.length)return false;
 return crypto.timingSafeEqual(Buffer.from(given),Buffer.from(key));
}
// Express middleware for the backend (3551) and the LAN bridge (3552).
export function directGuard(req,res,next){
 if(!fromInternet(req.socket?.remoteAddress))return next();
 const state=admit(req.socket?.remoteAddress,req.headers[DIRECT_HEADER],req.headers[NAME_HEADER]);
 if(state==='ok')return next();
 if(state==='wait')return res.status(403).type('text').send('Waiting for the host to let you in: ask them to press Allow in their Syndicate launcher, then try again.');
 if(state==='denied')return res.status(403).type('text').send('The host did not let you in.');
 res.status(403).end();
}
// Raw TCP (the chat relay on port 70): an internet connection must start with "SYNDICATE <key>\n".
export function directSocketKey(){return guestKey();}
export function checkSocketLine(remoteAddress,line){return !fromInternet(remoteAddress) || admit(remoteAddress,line.startsWith('SYNDICATE ')?line.slice(7).trim():'','')==='ok';}
// Headers a guest sends to a direct-play host: the join key and the player's name (shown in the host's Allow prompt).
export function guestHeaders(name){const k=guestKey();if(!k)return {};const h={[DIRECT_HEADER]:k};if(name)h[NAME_HEADER]=String(name).replace(/[^\w .~-]/g,'').slice(0,32);return h;}
