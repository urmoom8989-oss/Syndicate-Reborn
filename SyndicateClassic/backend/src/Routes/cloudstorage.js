import {cloudTextForClient} from '../Utils/syndicateLegacy111Lobby.js';
import {clientSettings} from '../Utils/syndicateClientSettings.js';
import {settingsForSeason} from '../Utils/syndicateToggleSprint.js';
import {withAccountLock} from '../Utils/syndicateAccountLock.js';
import express from "express";
import {clientBuild} from '../Utils/syndicateBuild.js';
import {runtimeOptionsWithIntro} from '../Utils/syndicateApolloIntro.js';
const app = express.Router();
import fs from "fs";
import crypto from "crypto";
import path from "path";
import { verifyToken } from "../User/tokenManager/tokenVerify.js";
import Utils from "../Utils/Utils.js";
import log from "../Utils/log.js";
import { dirname } from "dirname-filename-esm";
const __dirname = dirname(import.meta);
import Users from "../User/Mongodb/Schema/user.js";
import hotfixes from "../Utils/hotfixes.js";

//.Ini Stuff
app.get("/fortnite/api/cloudstorage/system", async (req, res) => {
  const dir = path.join(__dirname, "../", "CloudStorage");
  let CloudFiles = [];
  fs.readdirSync(dir).forEach((name) => {
    log.debug(`Found file: ${name}`);
    if (name.toLowerCase().endsWith(".ini")) {
      log.debug(`Found .ini file: ${name}`);
      let ParsedFile = cloudTextForClient(name, req.headers["user-agent"], hotfixes) ?? fs.readFileSync(path.join(dir, name)).toString();
      // Chapter 2 entrance (11.00, first time per account): same text as the file route below, so the hash matches.
      if (name.toLowerCase() === "defaultruntimeoptions.ini") ParsedFile = runtimeOptionsWithIntro(ParsedFile, req);
      const ParsedStats = fs.statSync(path.join(dir, name));
      CloudFiles.push({
        uniqueFilename: name,
        filename: name,
        hash: crypto.createHash("sha1").update(ParsedFile).digest("hex"),
        hash256: crypto.createHash("sha256").update(ParsedFile).digest("hex"),
        length: Buffer.byteLength(ParsedFile, "utf8"),
        contentType: "application/octet-stream",
        uploaded: ParsedStats.mtime,
        storageType: "Local",
        storageIds: {},
        doNotCache: true,
      });
    }
  });
  res.json(CloudFiles);
});
app.get("/fortnite/api/cloudstorage/system/:file", async (req, res) => {
  const fileName = req.params.file;
  if (req.params.file.includes("..")) return res.status(404).end();
  if (req.params.file.includes("~")) return res.status(404).end();
  if (req.params.file.includes(".env")) {
    let ip = req.ip;
    if (ip.startsWith("::ffff:")) {
      ip = ip.substring(7);
    }
    const user = Users.findOne({ ip });
    const username = user.username;

    if (user) {
      log.debug(`${username} tried to do the cloudstorage exploit! IP : ${ip}`);
      return res.status(404).end();
    } else if (!user) {
      log.debug(`${ip} tried to do the cloudstorage exploit!`);
      return res.status(404).end();
    }
  }
  if (fileName.toLowerCase() === "defaultgame.ini") {
    log.debug(`Sent "${fileName}" to user!`);
    return res.status(200).send(cloudTextForClient(fileName, req.headers["user-agent"], hotfixes));
  }
  if (fileName.toLowerCase() === "defaultengine.ini") {
    log.debug(`Sent "${fileName}" to user!`);
    return res.status(200).send(cloudTextForClient(fileName, req.headers["user-agent"], hotfixes));
  }
  if (fileName.toLowerCase() === "defaultruntimeoptions.ini") {
    log.debug(`Sent "${fileName}" to user!`);
    return res.status(200).send(runtimeOptionsWithIntro(cloudTextForClient(fileName, req.headers["user-agent"], hotfixes), req));
  }
  if (fileName.toLowerCase() === "defaultinput.ini") {
    log.debug(`Sent "${fileName}" to user!`);
    return res.status(200).send(cloudTextForClient(fileName, req.headers["user-agent"], hotfixes));
  } else {
    log.debug(`File: ${fileName} does not exist`);
    res.status(404).end();
  }
});
// Cloud saves are binary; metadata must describe the exact downloaded bytes.
// Settings slot per build. The Pre-season (1.7.2) and Season 1 (1.8.2) are both season 1, but they
// write different settings-file versions (12 and 15+): sharing one file made each build reject the
// other's keybinds ("Can't load version 12, older than 15"), so binds seemed not to save.
const settingsSlot=req=>clientBuild(req.headers['user-agent'])==='1.7.2'?0:Utils.GetVersion(req).season;
function owner(req,res){
 if((req.params.accountId||req.params[0])!==req.user.accountId){res.status(403).end();return false;}return true;
}
app.get('/fortnite/api/cloudstorage/user/*/:file',verifyToken,(req,res)=>{
 if(!owner(req,res))return;
 if(req.params.file.toLowerCase()!=='clientsettings.sav')return res.status(404).end();
 try{const bytes=settingsForSeason(clientSettings.read(req.user.accountId,settingsSlot(req)),settingsSlot(req));res.type('application/octet-stream').status(200).send(bytes||Buffer.alloc(0));}
 catch(error){res.status(400).json({error:error.message});}
});
app.get('/fortnite/api/cloudstorage/user/:accountId',verifyToken,(req,res)=>{
 if(!owner(req,res))return;
 try{
 const bytes=settingsForSeason(clientSettings.read(req.user.accountId,settingsSlot(req)),settingsSlot(req));
 if(!bytes)return res.json([]);
 res.json([{uniqueFilename:'ClientSettings.Sav',filename:'ClientSettings.Sav',
 hash:crypto.createHash('sha1').update(bytes).digest('hex'),hash256:crypto.createHash('sha256').update(bytes).digest('hex'),
 length:bytes.length,contentType:'application/octet-stream',uploaded:clientSettings.modified(req.user.accountId,settingsSlot(req)),
 storageType:'Local',storageIds:{},accountId:req.user.accountId,doNotCache:true}]);
 }catch(error){res.status(400).json({error:error.message});}
});
app.put('/fortnite/api/cloudstorage/user/*/:file',verifyToken,express.raw({type:()=>true,limit:4*1024*1024-1}),async(req,res)=>{
 if(!owner(req,res))return;
 if(req.params.file.toLowerCase()!=='clientsettings.sav')return res.status(404).end();
 try{await withAccountLock(req.user.accountId,async()=>{
 // Recheck after taking the same lock used when the host closes a travel session.
 const user=await Users.findOne({accountId:req.user.accountId}).lean();
 if(!user||(user.travelGuest&&user.travelGuest.status!=='active')){res.status(403).end();return;}
 clientSettings.save(req.user.accountId,settingsSlot(req),req.body);log.debug(`Saved settings and keybinds (${req.body.length} bytes) for ${user.username}`);res.status(204).end();
 });}catch(error){log.error('Could not save settings/keybinds: '+error.message);if(!res.headersSent)res.status(400).json({error:error.message});}
});
export default app;
