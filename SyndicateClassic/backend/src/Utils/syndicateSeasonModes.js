// Syndicate: which game modes each season offers, picked in the launcher (Settings > Game modes). The launcher writes
// Runtime/Server/season-modes.json:
//   {"version":1,"builds":{"7.40":["solo","duo","squad","creative"],"10.40":["solo","arenasolo"], ...}}
// A build that is not listed (or a missing/broken file) keeps the built-in defaults below, which are exactly the
// lobby lists Syndicate showed before this existed. The lobby playlist list (syndicateLegacy111Lobby.js), the
// matchmaker (Routes/matchmaking.js) and Save the World access (syndicateStwDev.js) all read it from here.
//
// Creative (Seasons 7 and up) and Save the World are experimental: turning them on opens the lobby button and sends
// the player to the match server, but the match servers do not run them properly yet (see the launcher's notes).
import fs from 'node:fs';
import {fileURLToPath} from 'node:url';
import {clientBuild} from './syndicateBuild.js';

const file = fileURLToPath(new URL('../../../Runtime/Server/season-modes.json', import.meta.url));
// Lobby order. playlist is the FrontEndPlaylistData / PlaylistOverrides name; Save the World has no BR playlist.
export const MODES = Object.freeze({
 solo:{playlist:'Playlist_DefaultSolo'}, duo:{playlist:'Playlist_DefaultDuo'}, squad:{playlist:'Playlist_DefaultSquad'},
 arenasolo:{playlist:'Playlist_ShowdownAlt_Solo'}, '50v50':{playlist:'Playlist_50v50'}, playground:{playlist:'Playlist_Playground'},
 creative:{playlist:'Playlist_PlaygroundV2'}, stw:{playlist:null},
});
const ORDER = Object.keys(MODES);
const BR = ['solo','duo','squad'];
const ARENA = new Set(['8.50','9.40','10.31','10.40','11.00','12.50']);
const CREATIVE = new Set(['7.40','8.50','9.40','10.31','10.40','11.00','12.50']); // Creative arrived in Season 7
const KNOWN = new Set(['1.7.2','1.8.2','1.11','2.5','3.5.1','4.5','5.10','6.20',...CREATIVE]);

// The modes a build can offer at all.
export function availableModes(build){
 if(!KNOWN.has(build))return [];
 const list=[...BR];
 if(ARENA.has(build))list.push('arenasolo');
 if(build==='4.5')list.push('50v50','playground');
 if(CREATIVE.has(build))list.push('creative');
 list.push('stw');
 return list;
}
// What the lobby showed before the launcher could choose (the Dev Kit also had Creative on 11.00).
export function defaultModes(build,env=process.env){
 if(!KNOWN.has(build))return [];
 const list=[...BR];
 if(ARENA.has(build))list.push('arenasolo');
 if(build==='4.5')list.push('50v50','playground');
 if(env.TILTED_DEVKIT==='1'&&build==='11.00')list.push('creative');
 return list;
}

let cache={at:0,mtime:-1,builds:{}};
function saved(){
 const now=Date.now();
 if(now-cache.at<2000)return cache.builds;
 cache.at=now;
 try{
  const mtime=fs.statSync(file).mtimeMs;
  if(mtime!==cache.mtime){const json=JSON.parse(fs.readFileSync(file,'utf8'));cache.mtime=mtime;cache.builds=json&&typeof json.builds==='object'&&json.builds?json.builds:{};}
 }catch{cache.mtime=-1;cache.builds={};}
 return cache.builds;
}
// The modes turned on for a build, in lobby order. At least one Battle Royale mode always stays on.
export function enabledModes(build,{builds=saved(),env=process.env}={}){
 const available=availableModes(build);if(!available.length)return [];
 const picked=Array.isArray(builds?.[build])?new Set(builds[build].map(m=>String(m).toLowerCase())):null;
 if(!picked)return defaultModes(build,env);
 const list=ORDER.filter(m=>available.includes(m)&&picked.has(m));
 if(!list.some(m=>MODES[m].playlist&&m!=='creative'))list.unshift('solo');
 return list;
}
// Matchmaker check. The 11.00 intro playlist is Fortnite's own entrance and always allowed; unknown builds keep working.
export function modeEnabled(build,mode,options){
 if(mode==='intro'||!availableModes(build).length)return true;
 return enabledModes(build,options).includes(mode);
}
// Save the World access for this client: the Dev Kit (as before) or the launcher's Save the World switch for its season.
export function stwEnabledFor(userAgent,options={}){
 const env=options.env||process.env;
 if(env.TILTED_DEVKIT==='1')return true;
 const build=clientBuild(userAgent||'');
 return !!build&&enabledModes(build,options).includes('stw');
}
export const modeName = mode => ({solo:'Solo',duo:'Duos',squad:'Squads',arenasolo:'Arena',['50v50']:'50v50',playground:'Playground',creative:'Creative',stw:'Save the World'})[mode]||mode;
