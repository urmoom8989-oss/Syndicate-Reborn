// Syndicate: durable login-token storage.
// The original backend only kept tokens in an in-memory KV store, so every
// backend restart (crash, repair, end-of-match service restart) silently logged
// out any Fortnite client that was still open. The next profile call (for
// example PurchaseCatalogEntry) then failed with "Authorization failed".
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const tokensPath = path.join(path.dirname(fileURLToPath(import.meta.url)), "../../tokens.json");
let timer = null;

function snapshot() {
  return JSON.stringify({
    accessTokens: global.accessTokens || [],
    refreshTokens: global.refreshTokens || [],
    clientTokens: global.clientTokens || [],
  }, null, 2);
}

export function writeTokensNow() {
  if (timer) { clearTimeout(timer); timer = null; }
  const temp = `${tokensPath}.${process.pid}.tmp`;
  try {
    fs.writeFileSync(temp, snapshot(), "utf8");
    fs.renameSync(temp, tokensPath);
  } catch (error) {
    try { fs.unlinkSync(temp); } catch {}
    console.error("[tokens] could not save tokens.json:", error.message);
  }
}

// Coalesce bursts of logins/refreshes into one small write.
export function scheduleTokenSave() {
  if (timer) return;
  timer = setTimeout(writeTokensNow, 250);
  timer.unref?.();
}

for (const signal of ["SIGINT", "SIGTERM", "beforeExit"]) {
  process.once(signal, () => { if (timer) writeTokensNow(); if (signal !== "beforeExit") process.exit(0); });
}
