const fs=require('fs'),path=require('path'),crypto=require('crypto');
function parseFill(value){return value===true||value==='true'||value==='1'||value===1;}
function requestedFill(query,mode){return mode!=='solo' && parseFill(query?.['player.option.fillTeam']);}
function writeSelection(dir,username,fill,mode,pid,now=Date.now()){
 if(typeof username!=='string'||!username.length||username.length>64||!Number.isInteger(pid)||pid<=0)return false;
 if(!['solo','duo','squad'].includes(mode))return false;
 const name=Buffer.from(username.toLowerCase(),'utf8').toString('hex');
 const folder=path.join(dir,'team-fill');fs.mkdirSync(folder,{recursive:true});
 const file=path.join(folder,name+'.txt'),tmp=file+'.'+crypto.randomUUID()+'.tmp';
 fs.writeFileSync(tmp,['pid='+pid,'expires='+Math.floor((now+600000)/1000),'mode='+mode,'fill='+(mode!=='solo'&&fill===true?'1':'0')].join('\n')+'\n');fs.renameSync(tmp,file);return true;
}
module.exports={parseFill,requestedFill,writeSelection};
