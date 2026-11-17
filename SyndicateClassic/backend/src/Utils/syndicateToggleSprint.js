import fs from 'node:fs';
import path from 'node:path';
// "Walk only, can't sprint": Pre-season to Season 4 have a Toggle Sprint setting (bToggleSprint). With it on,
// every press of the sprint key flips sprint on or off. The launcher's Sprint by default holds that key for you
// and presses it again after a jump or another key, so each of those presses switched sprint OFF and you walked.
// While Sprint by default is on, settings sent to those seasons get Toggle Sprint off (the game then saves it
// that way). Nothing else in the file changes.
const name=Buffer.from('bToggleSprint\0','latin1'),type=Buffer.from('BoolProperty\0','latin1');
export function sprintByDefaultOn(){
 try{return !!process.env.LOCALAPPDATA&&fs.existsSync(path.join(process.env.LOCALAPPDATA,'ProjectTilted','sprint-default-on.txt'));}catch{return false;}
}
// Serialized UE property: int32 name length, name, int32 type length, type, int64 size, then the bool byte.
export function withoutToggleSprint(bytes){
 if(!Buffer.isBuffer(bytes))return bytes;
 let out=bytes;
 for(let at=bytes.indexOf(name);at>=4;at=bytes.indexOf(name,at+1)){
  if(bytes.readInt32LE(at-4)!==name.length)continue;
  const t=at+name.length;
  if(t+4+type.length+9>bytes.length||bytes.readInt32LE(t)!==type.length||!bytes.subarray(t+4,t+4+type.length).equals(type))continue;
  const value=t+4+type.length+8;
  if(out[value]!==1)continue;
  if(out===bytes)out=Buffer.from(bytes);
  out[value]=0;
 }
 return out;
}
export function settingsForSeason(bytes,season,on=sprintByDefaultOn()){
 return on&&Number(season)<5?withoutToggleSprint(bytes):bytes;
}
