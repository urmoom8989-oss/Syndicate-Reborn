// Syndicate: LawinServer's Save the World data (profiles and responses), shared by syndicateStwDev.js and
// syndicateStwMcp.cjs. It ships with the launcher in Runtime\backend (Documents\Syndicate\Runtime\backend next to
// this backend's Syndicate-backend folder; Launcher/SyndicateLauncher/Runtime/backend in the repo).
"use strict";
const fs = require("fs");
const path = require("path");

const candidates = [
  path.join(__dirname, "../../../Runtime/backend"),
  path.join(__dirname, "../../../../Launcher/SyndicateLauncher/Runtime/backend"),
];
let root = null;
function lawinRoot() {
  if (root && fs.existsSync(path.join(root, "profiles/campaign.json"))) return root;
  root = candidates.find(dir => fs.existsSync(path.join(dir, "profiles/campaign.json")) &&
    fs.existsSync(path.join(dir, "responses/Campaign/cardPackData.json"))) || null;
  if (!root) throw new Error("LawinServer's Save the World data was not found (Runtime\\backend)");
  return root;
}

const cache = new Map();
// A response file (responses/<name>), read once. Callers that change it copy it first, as LawinServer does.
function lawinResponse(name) {
  if (!/^[A-Za-z0-9_\/.-]+\.json$/.test(name) || name.includes("..")) throw new Error("bad data name " + name);
  if (!cache.has(name)) cache.set(name, JSON.parse(fs.readFileSync(path.join(lawinRoot(), "responses", name), "utf8").replace(/^﻿/, "")));
  return cache.get(name);
}
// A fresh copy of one of LawinServer's full profiles (profiles/<id>.json).
function lawinProfile(id) {
  if (!/^[a-z0-9_]{1,64}$/i.test(id)) throw new Error("bad profile id " + id);
  return JSON.parse(fs.readFileSync(path.join(lawinRoot(), "profiles", id + ".json"), "utf8").replace(/^﻿/, ""));
}

module.exports = { lawinRoot, lawinResponse, lawinProfile };
