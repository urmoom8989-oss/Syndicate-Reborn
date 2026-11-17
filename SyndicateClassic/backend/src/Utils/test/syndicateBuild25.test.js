import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {clientBuild,isSeasonTwoBuild} from '../syndicateBuild.js';
import {project111Profile} from '../syndicateLegacy111Profile.js';
import {cloudTextForClient} from '../syndicateLegacy111Lobby.js';

const playlists=createRequire(import.meta.url)('../../../Matchmaker/playlists.cjs');
const ua='Fortnite/++Fortnite+Release-2.5-CL-3889387 Windows/10';

test('2.5 identifies the exact client without aliasing it to 1.11',()=>{
 for(const agent of [ua,'Fortnite/4.19-3889387+++Fortnite+Release-2.5','Fortnite/++Fortnite+Release-Cert-CL-3889387'])
  assert.equal(clientBuild(agent),'2.5');
 assert.equal(clientBuild('Fortnite/++Fortnite+Release-2.50-CL-9999999'),null);
 assert.equal(clientBuild('Fortnite/++Fortnite+Release-Cert-CL-38893870'),null);
 assert.equal(clientBuild('Fortnite/++Fortnite+Release-7.40-CL-5046157'),'7.40');
 assert.equal(clientBuild('Fortnite/++Fortnite+Release-Cert-CL-3807424'),'1.11');
 assert.ok(isSeasonTwoBuild('2.5')&&isSeasonTwoBuild('1.11'));
 assert.equal(isSeasonTwoBuild('3.5.1'),false);
});

test('2.5 profile response uses Season 2 and preserves equipped owned items without mutating storage',()=>{
 const profile={profileId:'athena',rvn:7,items:{
  pick:{templateId:'AthenaPickaxe:DefaultPickaxe',quantity:1,attributes:{}}
 },stats:{attributes:{season_num:7,favorite_pickaxe:'pick',favorite_dance:[]}}};
 const response=project111Profile(profile,'2.5');
 assert.equal(response.stats.attributes.season_num,2);
 assert.equal(response.stats.attributes.favorite_pickaxe,'pick');
 assert.equal(response.rvn,7);
 assert.equal(profile.stats.attributes.season_num,7);
 assert.equal(project111Profile(profile,'1.11').stats.attributes.season_num,2);
 assert.equal(project111Profile(profile,'1.8.2').stats.attributes.season_num,1);
});

test('2.5 lobby exposes supported Solo, Duo and Squad playlists',()=>{
 const config=cloudTextForClient('DefaultGame.ini',ua,{defaultgame:'[/Script/FortniteGame.FortGameInstance]\n'});
 for(const mode of ['Solo','Duo','Squad'])assert.match(config,new RegExp('Playlist_Default'+mode));
 assert.doesNotMatch(config,/Playlist_ShowdownAlt|Playlist_Playground/);
});

test('2.5 matchmaking accepts the matching host, keeps exact tickets and rejects mixed builds',()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'syndicate25-match-'));
 try{
  fs.writeFileSync(path.join(dir,'active-build.json'),JSON.stringify({Build:'2.5'}));
  assert.equal(playlists.requireBuild(clientBuild(ua),dir),'2.5');
  assert.throws(()=>playlists.requireBuild('1.11',dir));
  for(const mode of ['solo','duo','squad']){
   const modeDir=path.join(dir,mode);
   fs.mkdirSync(modeDir);
   fs.writeFileSync(path.join(modeDir,'active-build.json'),JSON.stringify({Build:'2.5'}));
   const ticket=playlists.reserve(mode,'test-'+mode,modeDir,Date.now(),'2.5');
   assert.equal(ticket.build,'2.5');
   assert.equal(ticket.mode,mode);
   assert.equal(playlists.readTicket(ticket.id,modeDir).build,'2.5');
  }
  fs.writeFileSync(path.join(dir,'active-build.json'),JSON.stringify({Build:'7.40'}));
  assert.throws(()=>playlists.requireBuild('2.5',dir));
 }finally{fs.rmSync(dir,{recursive:true,force:true});}
});
