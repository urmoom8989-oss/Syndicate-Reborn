import assert from "node:assert/strict";
import test from "node:test";
import hotfixes from "../hotfixes.js";
import { cloudTextForClient } from "../syndicateLegacy111Lobby.js";

function engineConfig(userAgent) {
  return cloudTextForClient("DefaultEngine.ini", userAgent, hotfixes);
}

test("Season 3 and later lobbies use the local WebSocket relay for party updates", () => {
  for (const release of ["3.5-CL-4000805", "8.50-CL-6058028"]) {
    const config = engineConfig("Fortnite/++Fortnite+Release-" + release);
    for (const section of ["Xmpp", "Xmpp Prod"]) {
      assert.ok(config.includes(
        `[OnlineSubsystemMcp.${section}]\nbUseSSL=false\nServerAddr="ws://127.0.0.1:70"\nServerPort=70`
      ));
    }
    assert.match(config, /\[XMPP\]\nbEnableWebsockets=true/);
  }
});

test("Season 1 and 1.11 keep the raw TLS party connection", () => {
  for (const release of ["1.8.2-CL-3741772", "Cert-CL-3807424"]) {
    const config = engineConfig("Fortnite/++Fortnite+Release-" + release);
    assert.match(config, /bUseSSL=true\nServerAddr="127\.0\.0\.1"\nServerPort=7071\nbUsePlainTextAuth=true/);
    assert.match(config, /\[XMPP\]\nbEnableWebsockets=false/);
    assert.doesNotMatch(config, /ws:\/\//);
  }
});
