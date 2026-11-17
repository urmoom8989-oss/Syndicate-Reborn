// Syndicate: Offline mode. The launcher starts the backend and matchmaker with SYNDICATE_OFFLINE=1 when the player
// turns Offline mode on. Then nothing here reaches the internet and nothing outside this PC can reach in:
//  - requests and sockets from any other machine are refused (loopback and this PC's own addresses only), and the
//    LAN bridge for friends (port 3552) is not started;
//  - "Play with friends" (Runtime/Server/remote-host.json) is ignored, so matches always run on this PC;
//  - the Discord bot, match telemetry uploads, redeem codes and party voice chat (Epic's RTC service) are off.
// Everything else (accounts, lockers, shops, progression, matches with bots) is local and works as normal.
import os from 'node:os';

export const OFFLINE = /^(1|true|yes|on)$/i.test(String(process.env.SYNDICATE_OFFLINE || '').trim());

let own = { at: 0, set: new Set() };
function ownAddresses() {
  const now = Date.now();
  if (now - own.at > 30000) {
    const set = new Set();
    try { for (const list of Object.values(os.networkInterfaces())) for (const entry of list || []) set.add(String(entry.address).toLowerCase()); } catch {}
    own = { at: now, set };
  }
  return own.set;
}
// True for a connection that starts on this PC: loopback, or one of this PC's own network addresses.
export function fromThisPc(address) {
  let a = String(address || '').toLowerCase();
  if (a.startsWith('::ffff:')) a = a.slice(7);
  if (a === '' || a === '::1' || /^127\./.test(a)) return true;
  return ownAddresses().has(a);
}
// Offline mode: anything from another machine is refused.
export const offlineRefuses = address => OFFLINE && !fromThisPc(address);
export const OFFLINE_REDEEM = 'Redeem codes need the internet. Turn off Offline mode in the launcher, then try again.';
