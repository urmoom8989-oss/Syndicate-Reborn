import {clientBuild} from './syndicateBuild.js';
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
  if(process.env.TILTED_DEVKIT==='1' && build==='11.00')g=g.replace(/^\+TextReplacements=.*NativeString="Creative".*$/gm,'');
  return g+`
[/Script/FortniteGame.FortGameInstance]
!FrontEndPlaylistData=ClearArray
+FrontEndPlaylistData=(PlaylistName=Playlist_DefaultSolo,PlaylistAccess=(bEnabled=true,bIsDefaultPlaylist=true,bVisibleWhenDisabled=true,CategoryIndex=0,DisplayPriority=0))
+FrontEndPlaylistData=(PlaylistName=Playlist_DefaultDuo,PlaylistAccess=(bEnabled=true,bIsDefaultPlaylist=false,bVisibleWhenDisabled=true,CategoryIndex=0,DisplayPriority=1))
+FrontEndPlaylistData=(PlaylistName=Playlist_DefaultSquad,PlaylistAccess=(bEnabled=true,bIsDefaultPlaylist=false,bVisibleWhenDisabled=true,CategoryIndex=0,DisplayPriority=2))
`+(['8.50','9.40','10.31','10.40','11.00','12.50'].includes(build)?`+FrontEndPlaylistData=(PlaylistName=Playlist_ShowdownAlt_Solo,PlaylistAccess=(bEnabled=true,bIsDefaultPlaylist=false,bVisibleWhenDisabled=true,CategoryIndex=0,DisplayPriority=3))
`:'')+(process.env.TILTED_DEVKIT==='1'&&build==='11.00'?`+FrontEndPlaylistData=(PlaylistName=Playlist_PlaygroundV2,PlaylistAccess=(bEnabled=true,bIsDefaultPlaylist=false,bVisibleWhenDisabled=true,bIsTournament=false,CategoryIndex=0,DisplayPriority=4))
`:'');}
 // Season 4 exposes PlaylistOverrides (PlaylistName, bEnabled, RegionsDisabled).
 // FrontEndPlaylistData is a legacy field and is absent in the 4.5 client.
 if(build==='4.5' && key==='defaultgame')return normal.replace('[/Script/FortniteGame.FortGameInstance]',
  '[/Script/FortniteGame.FortGameInstance]\n+PlaylistOverrides=(PlaylistName=Playlist_50v50,bEnabled=true)\n+PlaylistOverrides=(PlaylistName=Playlist_Playground,bEnabled=true)');
 if(!['1.11','2.5','1.8.2','1.7.2'].includes(build))return normal;
 if(key==='defaultgame')return `
[/Script/FortniteGame.FortGameInstance]
bBattleRoyaleMatchmakingEnabled=true
!FrontEndPlaylistData=ClearArray
+FrontEndPlaylistData=(PlaylistName=Playlist_DefaultSolo,PlaylistAccess=(bEnabled=true,bIsDefaultPlaylist=true,bVisibleWhenDisabled=true,bDisplayAsNew=false,CategoryIndex=0,bDisplayAsLimitedTime=false,DisplayPriority=0))
+FrontEndPlaylistData=(PlaylistName=Playlist_DefaultDuo,PlaylistAccess=(bEnabled=true,bIsDefaultPlaylist=false,bVisibleWhenDisabled=true,bDisplayAsNew=false,CategoryIndex=0,bDisplayAsLimitedTime=false,DisplayPriority=1))
+FrontEndPlaylistData=(PlaylistName=Playlist_DefaultSquad,PlaylistAccess=(bEnabled=true,bIsDefaultPlaylist=false,bVisibleWhenDisabled=true,bDisplayAsNew=false,CategoryIndex=0,bDisplayAsLimitedTime=false,DisplayPriority=2))
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
