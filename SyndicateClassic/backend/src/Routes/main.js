import { stwDevEnabled, stwWorldInfo } from '../Utils/syndicateStwDev.js';
import Profile from '../User/Mongodb/Schema/profiles.js';
import {careerStats,legacyCareer} from '../Utils/syndicateCareerCore.js';
import express from "express";
const app = express.Router();
import path from "path";
import fs from "fs";
import { dirname } from "dirname-filename-esm";
const __dirname = dirname(import.meta);
app.post("/fortnite/api/game/v2/chat/*/*/*/pc", (req, res) => {
  let resp = { GlobalChatRooms: [{ roomName: "Syndicateglobal" }] };
  res.json(resp);
});
app.post("/fortnite/api/game/v2/tryPlayOnPlatform/account/*", (req, res) => {
  res.setHeader("Content-Type", "text/plain");
  res.send(true);
});
app.get("/launcher/api/public/distributionpoints/", (req, res) => {
  res.json({
    distributions: [
      "https://download.epicgames.com/",
      "https://download2.epicgames.com/",
      "https://download3.epicgames.com/",
      "https://download4.epicgames.com/",
      "https://epicgames-download1.akamaized.net/",
    ],
  });
});
app.get("/launcher/api/public/assets/*", async (req, res) => {
  res.json({
    appName: "FortniteContentBuilds",
    labelName: "Syndicate",
    buildVersion: "++Fortnite+Release-20.00-CL-19458861-Windows",
    catalogItemId: "5cb97847cee34581afdbc445400e2f77",
    expires: "9999-12-31T23:59:59.999Z",
    items: {
      MANIFEST: {
        signature: "Syndicate",
        distribution: "https://Tilted.ol.epicgames.com/",
        path: "Builds/Fortnite/Content/CloudDir/Syndicate.manifest",
        hash: "55bb954f5596cadbe03693e1c06ca73368d427f3",
        additionalDistributions: [],
      },
      CHUNKS: {
        signature: "Syndicate",
        distribution: "https://Tilted.ol.epicgames.com/",
        path: "Builds/Fortnite/Content/CloudDir/Syndicate.manifest",
        additionalDistributions: [],
      },
    },
    assetId: "FortniteContentBuilds",
  });
});
app.get("/Builds/Fortnite/Content/CloudDir/*.manifest", async (req, res) => {
  res.set("Content-Type", "application/octet-stream");
  const manifest = fs.readFileSync(
    path.join(
      __dirname,
      "../",
      "CloudDir",
      fs.existsSync(path.join(__dirname, "../", "CloudDir", "Syndicate.manifest")) ? "Syndicate.manifest" : "Nebula.manifest"
    )
  );
  res.status(200).send(manifest).end();
});
app.get("/Builds/Fortnite/Content/CloudDir/*.chunk", async (req, res) => {
  res.set("Content-Type", "application/octet-stream");
  const chunk = fs.readFileSync(
    path.join(__dirname, "../", "CloudDir", fs.existsSync(path.join(__dirname, "../", "CloudDir", "Syndicate.chunk")) ? "Syndicate.chunk" : "Nebula.chunk")
  );
  res.status(200).send(chunk).end();
});
app.get("/Builds/Fortnite/Content/CloudDir/*.ini", async (req, res) => {
  const ini = fs.readFileSync(
    path.join(__dirname, "../../", "CloudDir", "Full.ini")
  );
  res.status(200).send(ini).end();
});
app.get("/waitingroom/api/waitingroom", (req, res) => {
  res.status(204);
  res.end();
});
app.get("/socialban/api/public/v1/*", (req, res) => {
  res.json({
    bans: [],
    warnings: [],
  });
});
app.get(
  "/fortnite/api/game/v2/events/tournamentandhistory/*/EU/WindowsClient",
  (req, res) => {
    res.json({});
  }
);
async function careerAttributes(accountId) {
  const doc=await Profile.collection.findOne({accountId},{projection:{'profiles.athena.stats.attributes.tilted_career':1}});
  return doc?.profiles?.athena?.stats?.attributes || {};
}
const careerV2=async(req,res,next)=>{try{res.json({startTime:0,endTime:2147483647,accountId:req.params.accountId,stats:careerStats(await careerAttributes(req.params.accountId))});}catch(e){next(e);}};
app.get('/fortnite/api/statsv2/account/:accountId',careerV2);
app.get('/statsproxy/api/statsv2/account/:accountId',careerV2);
app.get('/fortnite/api/stats/accountId/:accountId/bulk/window/alltime',async(req,res,next)=>{try{res.json(legacyCareer(req.params.accountId,await careerAttributes(req.params.accountId)));}catch(e){next(e);}});
app.post("/fortnite/api/feedback/*", (req, res) => {
  res.status(200);
  res.end();
});


app.post("/fortnite/api/game/v2/events/v2/setSubgroup/*", (req, res) => {
  res.status(204);
  res.end();
});
app.get("/fortnite/api/game/v2/enabled_features", (req, res) => {
  res.json([]);
});
app.get("/api/v1/events/Fortnite/download/*", (req, res) => {
  res.json({});
});
app.post(
  "/api/v1/assets/Fortnite/*/*",
  async (req, res) => {
    /*if (req.body.hasOwnProperty("FortCreativeDiscoverySurface") && req.body.FortCreativeDiscoverySurface == 0) {
        const discovery_api_assets = require("../../responses/Athena/Discovery/discovery_api_assets.json");
        res.json(discovery_api_assets)
    }
    else {*/
    res.json({
      FortCreativeDiscoverySurface: {
        meta: {
          promotion: req.body.FortCreativeDiscoverySurface || 0,
        },
        assets: {},
      },
    });
  }
  /*}*/
);
app.get("/fortnite/api/game/v2/twitch/*", (req, res) => {
  res.status(200);
  res.end();
});
app.get("/fortnite/api/game/v2/world/info", (req, res) => {
  // Dev launcher: the Save the World theaters and missions (LawinServer's data); otherwise empty as before.
  res.json(stwDevEnabled() ? stwWorldInfo() : {});
});
app.post(
  "/fortnite/api/game/v2/chat/*/recommendGeneralChatRooms/pc",
  (req, res) => {
    res.json({});
  }
);
app.get("/fortnite/api/receipts/v1/account/*/receipts", (req, res) => {
  res.json([]);
});
app.get("/fortnite/api/game/v2/leaderboards/cohort/*", (req, res) => {
  res.json([]);
});
app.post("/datarouter/api/v1/public/data", (req, res) => {
  res.status(204);
  res.end();
});
app.post("/api/v1/user/setting", async (req, res) => {
  res.json([]);
})
export default app;
//# sourceMappingURL=main.js.map
for(const route of ['/fortnite/api/statsv2/query','/statsproxy/api/statsv2/query'])app.post(route,async(req,res,next)=>{try{const ids=Array.isArray(req.body?.owners)?req.body.owners:[];if(ids.length>100||ids.some(id=>typeof id!=='string'))return res.status(400).json({error:'Invalid owners'});res.json(await Promise.all(ids.map(async accountId=>({accountId,startTime:0,endTime:2147483647,stats:careerStats(await careerAttributes(accountId))}))));}catch(e){next(e);}});
