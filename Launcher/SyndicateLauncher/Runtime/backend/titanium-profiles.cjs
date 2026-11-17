"use strict";
const fs=require("fs"),path=require("path");
// The executable's embedded JSON is a template, not a persistent account.
// The launcher gives each profile a working directory outside the embedded
// filesystem's executable directory; use absolute paths into that directory.
const root=path.resolve(process.cwd(),"profiles"),cache=new Map();
const originalWrite=fs.writeFileSync.bind(fs);
function profilePath(name){
    name=String(name).replace(/\.json$/i,"");
    if(!/^[a-z0-9_]{1,64}$/i.test(name))throw new Error("Invalid profile identifier.");
    return path.join(root,name+".json");
}
fs.writeFileSync=function(file,data,options){
    if(typeof file!=="string")return originalWrite(file,data,options);
    const full=path.resolve(file);
    if(path.dirname(full)!==root||path.extname(full)!==".json")return originalWrite(file,data,options);
    // Publish complete JSON atomically. Interrupted writes leave the previous
    // locker available, rather than a truncated profile on the next launch.
    const parsed=JSON.parse(Buffer.isBuffer(data)?data.toString("utf8"):String(data));
    const temporary=full+".pending-"+process.pid;
    try{originalWrite(temporary,JSON.stringify(parsed,null,2),"utf8");fs.renameSync(temporary,full);}
    finally{if(fs.existsSync(temporary))fs.unlinkSync(temporary);}
};
module.exports=function(name){
    const file=profilePath(name);
    if(!cache.has(file))cache.set(file,JSON.parse(fs.readFileSync(file,"utf8").replace(/^\uFEFF/,"")));
    return cache.get(file);
};
