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
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {clientBuild} from './syndicateBuild.js';

const file = fileURLToPath(new URL('../../../Runtime/Server/season-modes.json', import.meta.url));
// Lobby order. playlist is the FrontEndPlaylistData / PlaylistOverrides name; Save the World has no BR playlist.
export const MODES = Object.freeze({
 solo:{playlist:'Playlist_DefaultSolo'}, duo:{playlist:'Playlist_DefaultDuo'}, squad:{playlist:'Playlist_DefaultSquad'},
 arenasolo:{playlist:'Playlist_ShowdownAlt_Solo'},
 '50v50':{playlist:'Playlist_50v50'},
 gauntlet:{playlist:'Playlist_Ashton_Sm'},
 solidgold:{playlist:'Playlist_SolidGold_Solo'},
 blitz:{playlist:'Playlist_Blitz_Solo'},
 snipers:{playlist:'Playlist_Snipers_Solo'},
 close:{playlist:'Playlist_Close_Solo'},
 highexplosives:{playlist:'Playlist_HighExplosives_Squads'},
 disco:{playlist:'Playlist_Disco_32'},
 teamrumble:{playlist:'Playlist_Respawn_24'},
 foodfight:{playlist:'Playlist_Barrier_16'},
 floorislava:{playlist:'Playlist_Respawn_20_Lava'},
 arenaduos:{playlist:'Playlist_ShowdownAlt_Duos'},
 playground:{playlist:'Playlist_Playground'}, creative:{playlist:'Playlist_PlaygroundV2'}, stw:{playlist:null},
});
// Limited-time modes and the builds that have them (launcher Services/SeasonModes.cs keeps the same list).
const LTM = {
 '50v50':['1.11', '2.5', '3.5.1', '4.5', '5.10', '6.20'],
 gauntlet:['4.5'],
 solidgold:['3.5.1', '4.5', '5.10', '6.20'],
 blitz:['3.5.1', '4.5', '5.10', '6.20'],
 snipers:['3.5.1', '4.5', '5.10', '6.20', '7.40', '8.50', '9.40'],
 close:['4.5', '5.10', '6.20', '7.40', '8.50', '9.40'],
 highexplosives:['5.10', '6.20', '7.40', '8.50', '9.40'],
 disco:['6.20', '7.40'],
 teamrumble:['7.40', '8.50', '9.40', '10.31', '10.40', '11.00', '12.50'],
 foodfight:['7.40', '8.50', '9.40'],
 floorislava:['8.50', '9.40'],
 arenaduos:['8.50', '9.40', '12.50'],
};
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
 for(const [mode,builds] of Object.entries(LTM))if(builds.includes(build))list.push(mode);
 if(build==='4.5')list.push('playground');
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

let cache={at:0,mtime:-1,builds:{},layout:{}};
function load(){
 const now=Date.now();
 if(now-cache.at<2000)return cache;
 cache.at=now;
 try{
  const mtime=fs.statSync(file).mtimeMs;
  if(mtime!==cache.mtime){
   const json=JSON.parse(fs.readFileSync(file,'utf8'));cache.mtime=mtime;
   cache.builds=json&&typeof json.builds==='object'&&json.builds?json.builds:{};
   cache.layout=json&&typeof json.layout==='object'&&json.layout?json.layout:{};
  }
 }catch{cache.mtime=-1;cache.builds={};cache.layout={};}
 return cache;
}
const saved=()=>load().builds;
const savedLayouts=()=>load().layout;
// Pictures the player put on lobby tiles (launcher lobby editor). Only files in the launcher's own picture folder
// (%APPDATA%\SyndicateLauncher\ModeImages) are served, whatever season-modes.json says.
const IMAGE_DIR=process.env.APPDATA?path.join(process.env.APPDATA,'SyndicateLauncher','ModeImages'):'';
export function modeImage(build,mode,{layouts=savedLayouts()}={}){
 const images=layouts?.[build]?.images;
 if(!IMAGE_DIR||!images||typeof images!=='object')return null;
 const key=Object.keys(images).find(k=>k.toLowerCase()===String(mode).toLowerCase());
 const file=key&&typeof images[key]==='string'?path.resolve(images[key]):'';
 if(!file||!file.toLowerCase().startsWith(IMAGE_DIR.toLowerCase()+path.sep))return null;
 try{return fs.statSync(file).isFile()?file:null;}catch{return null;}
}
export function modeImages(build,opts){
 return Object.keys(MODES).filter(m=>MODES[m].playlist).map(m=>({mode:m,playlist:MODES[m].playlist,file:modeImage(build,m,opts)})).filter(x=>x.file);
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
// Rows in Fortnite's mode selector (FrontEndPlaylistData CategoryIndex). Season X's screen
// (AthenaMatchmakingOptionsDisplay_v2) has three: StandardGameModeBox (0), LTMGameModeBox (1) and LTMGameModeBox2 (2).
// Seasons 5 to X get three rows, other seasons the first two; Season 4 lists PlaylistOverrides, which have no rows. Keep this in step with the
// launcher (Services/SeasonModes.cs RowCount).
const THREE_ROWS = new Set(['5.10','6.20','7.40','8.50','9.40','10.31','10.40']);
export function lobbyRowCount(build){
 if(!KNOWN.has(build))return 0;
 if(build==='4.5')return 1;
 return THREE_ROWS.has(build)?3:2;
}
// Launcher Settings > Game modes > lobby layout: the tiles Fortnite's mode selector shows, in order. Modes that are on,
// plus modes the player chose to show locked (greyed out, can't be picked). One tile is the default; a tile can carry
// a NEW or LIMITED TIME badge. Save the World is a lobby button, not a tile. With no saved layout this is exactly
// the old list: the modes that are on, in lobby order, the first one default, no badges.
// Each tile sits in a row (rows:{mode:0|1|2}, top row when missing); the lobby lists the rows top to bottom.
// layout[build] = {order:[modes], default:mode, badges:{mode:'new'|'limited'}, locked:[modes], rows:{mode:row}}.
export function lobbyTiles(build,{builds=saved(),layouts=savedLayouts(),env=process.env}={}){
 const lower=v=>String(v||'').toLowerCase();
 const available=availableModes(build).filter(m=>MODES[m].playlist);
 const enabled=enabledModes(build,{builds,env}).filter(m=>MODES[m].playlist);
 const layout=layouts&&typeof layouts[build]==='object'&&layouts[build]?layouts[build]:{};
 const locked=new Set((Array.isArray(layout.locked)?layout.locked:[]).map(lower).filter(m=>available.includes(m)&&!enabled.includes(m)));
 const order=Array.isArray(layout.order)?layout.order.map(lower):[];
 const rank=m=>{const i=order.indexOf(m);return i<0?order.length+ORDER.indexOf(m):i;};
 const maxRow=Math.max(1,lobbyRowCount(build))-1;
 const rows={};
 if(layout.rows&&typeof layout.rows==='object')for(const [m,r] of Object.entries(layout.rows))rows[lower(m)]=r;
 const rowOf=m=>{const r=Number(rows[m]);return Number.isInteger(r)&&r>0?Math.min(r,maxRow):0;};
 const shown=available.filter(m=>enabled.includes(m)||locked.has(m)).sort((a,b)=>rowOf(a)-rowOf(b)||rank(a)-rank(b));
 const wanted=lower(layout.default);
 const fallback=shown.find(m=>enabled.includes(m));
 const def=enabled.includes(wanted)?wanted:fallback;
 const badges=layout.badges&&typeof layout.badges==='object'?layout.badges:{};
 return shown.map(m=>({mode:m,playlist:MODES[m].playlist,enabled:enabled.includes(m),isDefault:m===def,row:rowOf(m),
  badge:['new','limited'].includes(lower(badges[m]))?lower(badges[m]):''}));
}
export const modeName = mode => ({solo:'Solo',duo:'Duos',squad:'Squads',arenasolo:'Arena',['50v50']:'50v50',playground:'Playground',creative:'Creative',stw:'Save the World'})[mode]||mode;
