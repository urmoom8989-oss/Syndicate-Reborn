import {clientBuild} from './syndicateBuild.js';
import {enabledModes,lobbyTiles} from './syndicateSeasonModes.js';
// Launcher Settings > Game modes (syndicateSeasonModes.js): the lobby lists the modes turned on for the build.
// The Save the World and Creative buttons stay relabelled UNAVAILABLE unless they are turned on.
const PLAYLIST={solo:'Playlist_DefaultSolo',duo:'Playlist_DefaultDuo',squad:'Playlist_DefaultSquad','50v50':'Playlist_50v50',playground:'Playlist_Playground'};
const STW_BUTTON='F1B39B41400513DFDBBC39BE41A2AA7A',CREATIVE_BUTTON='85CC70F746437FF312DF528927FD1F52';
// One +TextReplacements entry by key. "Save The\r\nWorld" spans two lines, so match up to the closing ))).
const dropText=(text,key)=>text.replace(new RegExp('^\\+TextReplacements=[^\\n]*Key="'+key+'"[\\s\\S]*?\\)\\)\\)[ \\t]*\\r?$\\n?','m'),'');
function openButtons(text,modes){
 if(modes.includes('stw'))text=dropText(text,STW_BUTTON);
 if(modes.includes('creative'))text=dropText(text,CREATIVE_BUTTON);
 return text;
}
// One FrontEndPlaylistData line per lobby tile (row, order, default, locked, badges: syndicateSeasonModes.js lobbyTiles).
// CategoryIndex is the row of the mode selector the tile sits in.
const badgeFields=t=>(t.badge==='new'?'bDisplayAsNew=true,':'')+(t.badge==='limited'?'bDisplayAsLimitedTime=true,':'');
const tileLine=(t,i)=>`+FrontEndPlaylistData=(PlaylistName=${t.playlist},PlaylistAccess=(bEnabled=${t.enabled},bIsDefaultPlaylist=${t.isDefault},bVisibleWhenDisabled=true,${badgeFields(t)}${t.mode==='creative'?'bIsTournament=false,':''}CategoryIndex=${t.row||0},DisplayPriority=${i}))`;
const legacyTileLine=(t,i)=>`+FrontEndPlaylistData=(PlaylistName=${t.playlist},PlaylistAccess=(bEnabled=${t.enabled},bIsDefaultPlaylist=${t.isDefault},bVisibleWhenDisabled=true,bDisplayAsNew=${t.badge==='new'},CategoryIndex=${t.row||0},bDisplayAsLimitedTime=${t.badge==='limited'},DisplayPriority=${i}))`;
// Keep Cert 3807424 on its own native lobby/playlist configuration.
export function cloudTextForClient(file,userAgent,hotfixes){
 const key=file.toLowerCase().replace(/\.ini$/,'');
 const normal=hotfixes[key];
 const build=clientBuild(userAgent);
 // 1.7.2 / 1.8.2 / 1.11 use raw XMPP on 7071 with TLS (the adapter now always has a certificate); without
 // it Season 2 (1.11) never logged in to chat at all: friends showed offline on both PCs and invites failed.
 // Without TLS they never sent their login and parties/ready-up hung on "Waiting for players".
 // Pre-season (1.7.2): auto exposure (eye adaptation) can run away on current GPUs and drivers, blowing the whole
 // screen out to white/green glare. A fixed exposure keeps the normal look.
 const exposure=build==='1.7.2'?'\n[ConsoleVariables]\nr.EyeAdaptationQuality=0\n':'';
 if(['1.7.2','1.8.2','1.11'].includes(build) && key==='defaultengine')return exposure+(normal?.split('[/Script/Qos.QosRegionManager]')[0]||'')
  .replaceAll('bUseSSL=false','bUseSSL=true')
  .replaceAll('bEnableWebsockets=true','bEnableWebsockets=false')
  .replace(/ServerAddr=.*$/gm,'ServerAddr="127.0.0.1"')
  .replace(/ServerPort=.*$/gm,'ServerPort=7071\nbUsePlainTextAuth=true');
 // Seasons 3 and 5-10 get an explicit Solo/Duo/Squad lobby list (unused fields are ignored). Season 3
 // used the shared list, which had Squads switched off and an Arena entry that build does not have.
 if(['2.5','3.5.1','5.10','6.20','7.40','8.50','9.40','10.31','10.40','11.00','12.50'].includes(build) && key==='defaultgame'){let g=normal.replace(/^\+?FrontEndPlaylistData=.*$/gm,'');
  // Dev Kit 11.00: drop the "Creative -> UNAVAILABLE" relabel so the Creative button is clickable to
  // experiment. Island streaming is still broken on UE4, so expect empty sea until that's solved.
  // Creative (Seasons 7+) and Save the World open their lobby buttons when turned on in the launcher. Creative's
  // island streaming is still broken on UE4, so expect empty sea until that's solved.
  const modes=enabledModes(build);
  g=openButtons(g,modes);
  return g+`
[/Script/FortniteGame.FortGameInstance]
!FrontEndPlaylistData=ClearArray
`+lobbyTiles(build).map((t,i)=>tileLine(t,i)+'\n').join('');}
 // Season 4 exposes PlaylistOverrides (PlaylistName, bEnabled, RegionsDisabled).
 // FrontEndPlaylistData is a legacy field and is absent in the 4.5 client. Solo, Duos and Squads are on in the client
 // already; 50v50 and Playground are switched on here, and anything turned off in the launcher is switched off.
 if(build==='4.5' && key==='defaultgame'){
  const modes=enabledModes(build),lines=[];
  for(const m of ['solo','duo','squad','50v50','playground']){
   const on=modes.includes(m);
   if(!on||m==='50v50'||m==='playground')lines.push(`+PlaylistOverrides=(PlaylistName=${PLAYLIST[m]},bEnabled=${on})`);
  }
  return openButtons(normal,modes).replace('[/Script/FortniteGame.FortGameInstance]','[/Script/FortniteGame.FortGameInstance]\n'+lines.join('\n'));
 }
 if(!['1.11','2.5','1.8.2','1.7.2'].includes(build))return normal;
 if(key==='defaultgame')return `
[/Script/FortniteGame.FortGameInstance]
bBattleRoyaleMatchmakingEnabled=true
!FrontEndPlaylistData=ClearArray
${lobbyTiles(build).map(legacyTileLine).join('\n')}
[/Script/FortniteGame.FortChatManager]
bShouldRequestGeneralChatRooms=false
bShouldJoinGlobalChat=false
bShouldJoinFounderChat=false
bIsAthenaGlobalChatEnabled=false
[/Script/Account.OnlineAccountCommon]
bEnableWaitingRoom=false
bRequireLightswitchAtStartup=false
AccessGrantDelaySeconds=0.0
`;
 if(key==='defaultruntimeoptions')return '; Use built-in 1.11 runtime options.\n';
 // The old Qos manager has a different region schema; keep its packaged defaults.
 if(key==='defaultengine')return normal?.split('[/Script/Qos.QosRegionManager]')[0];
 return normal;
}
