import { isBrokenPipe } from "./Utils/syndicateConsoleLog.js"; // first: own log file, broken output never stops the backend
import {startLegacyXmpp} from './Utils/syndicateLegacyXmpp.js';
import {proxyLobby,startSocialRelay} from './Utils/syndicateTravel.js';
import {directGuard} from './Utils/syndicateDirect.js';
import express from "express";
import mongoose from "mongoose";
import fs from "fs";
import path from "path";
import jwt from "jsonwebtoken";
import { dirname } from "dirname-filename-esm";
import destr from "destr";
import kv from "./Utils/kv.js";
import { importLegacyAccounts } from "./Utils/syndicateLegacyImport.js";
import Utils from "./Utils/Utils.js";
import log from "./Utils/log.js";
import { DateAddHours } from "./Routes/auth.js";
import "./discord/index.js";
import { v4 } from "uuid";

import dotenv from "dotenv";
dotenv.config();

// Syndicate: one unexpected error (a bad request, an XMPP message, a timer) used to stop the whole backend,
// which signed every open Fortnite back out to the Epic login screen. Log it and keep serving instead.
// Reporting an error must never cause another one (a broken output pipe used to make this loop forever).
let reportingError = false;
function reportError(label, error) {
  if (isBrokenPipe(error) || reportingError) return;
  reportingError = true;
  try { console.error(label, error && error.stack ? error.stack : error); } catch {}
  finally { reportingError = false; }
}
process.on("uncaughtException", (error) => reportError("[Syndicate] Unexpected error (backend kept running):", error));
process.on("unhandledRejection", (reason) => reportError("[Syndicate] Unhandled promise rejection (backend kept running):", reason));

const __dirname = dirname(import.meta);

const resolveJwtSecret = () => {
  if (process.env.JWT_SECRET && process.env.JWT_SECRET.trim()) {
    return process.env.JWT_SECRET.trim();
  }

  const jwtSecretPath = path.join(__dirname, "../jwt.secret");

  if (fs.existsSync(jwtSecretPath)) {
    const existing = fs.readFileSync(jwtSecretPath, "utf8").trim();
    if (existing) {
      return existing;
    }
  }

  const generated = v4().replace(/-/gi, "") + v4().replace(/-/gi, "");
  fs.writeFileSync(jwtSecretPath, generated, "utf8");
  return generated;
};

global.kv = kv;
global.JWT_SECRET = resolveJwtSecret();
global.accessTokens = [];
global.refreshTokens = [];
global.clientTokens = [];
global.smartXMPP = false;
global.exchangeCodes = [];
const app = express();
// Direct play: requests from the internet need the host's join key (see Utils/syndicateDirect.js).
app.use(directGuard);
app.disable("x-powered-by");
app.set("etag", false);
const PORT = process.env.PORT;
let redisTokens;
let tokens;
try {
  tokens = destr(fs.readFileSync(path.join(__dirname, "../tokens.json")).toString());
} catch {
  tokens = {};
}
// Syndicate: drop expired or foreign tokens without skipping entries
// (the old loop spliced while iterating and missed every other bad token).
if (!tokens || typeof tokens !== "object") tokens = {};
for (const tokenType of ["accessTokens", "refreshTokens", "clientTokens"]) {
  const list = Array.isArray(tokens[tokenType]) ? tokens[tokenType] : [];
  tokens[tokenType] = list.filter((entry) => {
    try {
      const decodedToken = jwt.verify(String(entry.token).replace("eg1~", ""), global.JWT_SECRET);
      return DateAddHours(new Date(decodedToken.creation_date), decodedToken.hours_expire).getTime() > Date.now();
    } catch {
      return false;
    }
  });
}
fs.writeFileSync(
  path.join(__dirname, "../tokens.json"),
  JSON.stringify(tokens, null, 2) || ""
);
if (!tokens || !tokens.accessTokens) {
  console.log("No access tokens found, resetting tokens.json");
  await kv.set(
    "tokens",
    fs.readFileSync(path.join(__dirname, "../tokens.json")).toString()
  );
  tokens = destr(
    fs.readFileSync(path.join(__dirname, "../tokens.json")).toString()
  );
}
global.accessTokens = tokens.accessTokens;
global.refreshTokens = tokens.refreshTokens;
global.clientTokens = tokens.clientTokens;
mongoose.set("strictQuery", true);
mongoose
  // Syndicate: local single-user database; keep a small warm pool and fail fast.
  .connect(process.env.MONGO_URI, { maxPoolSize: 20, minPoolSize: 2, serverSelectionTimeoutMS: 5000 })
  .then(() => {
    log.database("Connected to MongoDB");
    importLegacyAccounts().catch((error) => console.warn("[Syndicate] Player import skipped:", error.message));
  })
  .catch((error) => {
    console.error("Error connecting to MongoDB: ", error);
  });
mongoose.connection.on("error", (err) => {
  // Syndicate: mongoose reconnects by itself. Throwing here crashed the backend,
  // which logged out every open game client.
  log.error(
    "MongoDB connection problem (will retry): " + (err && err.message ? err.message : err)
  );
});
app.get("/", (req, res) => {
  res.status(200).json({
    status: "ok",
    uptime: process.uptime(),
    // Syndicate: tells the launcher this backend writes its own log (older ones depend on the launcher that
    // started them and freeze once it closes) and whether the database is connected.
    ownLog: true,
    db: mongoose.connection.readyState === 1,
  });
});
app.all("/unknown", (req, res) => {
  res.status(200).send("reboot launcher ok");
});
app.use(proxyLobby);
app.use(express.json());
app.use(express.urlencoded({ extended: true }));
const importRoutes = async (dir) => {
  for (const fileName of fs.readdirSync(path.join(__dirname, dir))) {
    if (fileName.includes(".map") || !fileName.endsWith(".js")) continue; // skip backups like Fortnite.js.before-*
    try {
      app.use((await import(`file://${__dirname}/${dir}/${fileName}`)).default);
    } catch (error) {
      console.log(fileName, error);
    }
  }
};
await importRoutes("Routes");
app
  .listen(PORT, () => {
    log.backendstart(`Backend started listening on port ${PORT}`);
    process.env.XMPP_PORT="7070";
    import("./xmpp/xmpp.js").then(()=>{startSocialRelay();startLegacyXmpp();});
  })
  .on("error", async (err) => {
    if (err.message == "EADDRINUSE") {
      log.error(`Port ${PORT} is already in use!\nClosing in 3 seconds...`);
      process.exit(0);
    } else throw err;
  });
const loggedUrls = new Set();
app.use((req, res, next) => {
  const url = req.originalUrl;
  if (!loggedUrls.has(url)) {
    log.debug(
      `Missing endpoint: ${req.method} ${url} request port ${req.socket.localPort}`
    );
    Utils.createError(
      "errors.com.epicgames.common.not_found",
      "Sorry the resource you were trying to find could not be found",
      undefined,
      1004,
      undefined,
      404,
      res
    );
  }
  next();
});