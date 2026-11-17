import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
// Keybinds and game settings used to live next to the backend code. The public release
// replaces that folder with every new version (and each install had its own copy), so
// binds were lost. They now live in the shared player data folder, like accounts.
const legacyDirectory=fileURLToPath(new URL('../ClientSettings/',import.meta.url));
function sharedDirectory(){
 if(process.env.TILTED_SETTINGS_DIR)return process.env.TILTED_SETTINGS_DIR;
 if(process.platform==='win32'&&process.env.LOCALAPPDATA)return path.join(process.env.LOCALAPPDATA,'ProjectTilted','UserData','ClientSettings');
 return legacyDirectory;
}
const directory=sharedDirectory();
// Newer seasons write larger settings files than 400 KB.
const limit=4*1024*1024;
export function createSettingsStore(dir=directory,legacy=dir===legacyDirectory?null:legacyDirectory){
 function checkId(id){if(!/^[a-f0-9]{32}$/i.test(id))throw Error('Invalid settings account.');}
 function file(id,season){checkId(id);if(!/^\d{1,3}$/.test(String(season)))throw Error('Invalid settings season.');return path.join(dir,`ClientSettings-${id}-${season}.Sav`);}
 function readAt(p){try{return fs.readFileSync(p);}catch(e){if(e.code==='ENOENT')return null;throw e;}}
 function read(id,season){const p=file(id,season);const bytes=readAt(p);if(bytes||!legacy)return bytes;
  // One-time move of settings saved by older versions.
  const old=readAt(path.join(legacy,path.basename(p)));if(old?.length){try{fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(p+'.tmp',old);fs.renameSync(p+'.tmp',p);}catch{}}return old;}
 function save(id,season,bytes){const p=file(id,season);if(!Buffer.isBuffer(bytes)||bytes.length===0||bytes.length>=limit)throw Error('Settings file must be nonempty and less than 4 MB.');const existing=read(id,season);if(existing?.equals(bytes))return;fs.mkdirSync(dir,{recursive:true});if(existing?.length){fs.writeFileSync(p+'.bak.tmp',existing);fs.renameSync(p+'.bak.tmp',p+'.bak');}fs.writeFileSync(p+'.tmp',bytes);fs.renameSync(p+'.tmp',p);}
 function validate(bundle){
  if(!bundle||bundle.version!==1||!Array.isArray(bundle.files)||bundle.files.length>32)throw Error('Invalid settings transfer.');
  const seen=new Set();
  for(const item of bundle.files){
   if(!item||!/^\d{1,3}$/.test(item.season)||seen.has(item.season)||typeof item.data!=='string'||item.data.length>Math.ceil(limit/3)*4+4)throw Error('Invalid settings file.');
   seen.add(item.season);const bytes=Buffer.from(item.data,'base64');
   if(bytes.length>=limit||bytes.toString('base64')!==item.data)throw Error('Invalid settings encoding or size.');
  }
 }
 function snapshot(id){
  checkId(id);const files=[];
  if(fs.existsSync(dir))for(const name of fs.readdirSync(dir).sort()){
   const match=name.match(new RegExp(`^ClientSettings-${id}-(\\d{1,3})\\.Sav$`,'i'));
   if(match)files.push({season:match[1],data:fs.readFileSync(path.join(dir,name)).toString('base64')});
  }
  const bundle={version:1,files};validate(bundle);return bundle;
 }
 function apply(id,bundle){checkId(id);validate(bundle);for(const item of bundle.files){const bytes=Buffer.from(item.data,'base64');if(bytes.length)save(id,item.season,bytes);}}
 function modified(id,season){try{return fs.statSync(file(id,season)).mtime.toISOString();}catch{return new Date().toISOString();}}
 return {read,save,snapshot,apply,validate,modified};
}
export const clientSettings=createSettingsStore();
