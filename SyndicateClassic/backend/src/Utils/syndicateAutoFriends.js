// Players who share a Syndicate backend (the host's own players, Radmin guests whose account travelled to
// this host, and players on the same LAN using this host) become Fortnite friends automatically, so
// they can see each other and join parties without sending requests. Turn off with TILTED_AUTO_FRIENDS=0.
// Also: parties are always joinable between these players (Pre-season showed friends' parties as locked).

export const autoFriendsEnabled = () => process.env.TILTED_AUTO_FRIENDS !== '0';

// Befriends `accountId` with every other player connected to this backend's XMPP. Returns the ids added.
export async function befriendConnected(accountId, clients = global.Clients || [], api = null, store = null) {
  if (!autoFriendsEnabled() || !accountId) return [];
  // Loaded on use so the presence helpers below work (and are testable) without the database layer.
  if (!api) api = (await import('./friend.js')).friends;
  if (!store) store = (await import('../User/Mongodb/Schema/friends.js')).default;
  const added = [];
  const others = [...new Set(clients.filter(c => c && !c.isPartyBot && c.accountId && c.accountId !== accountId).map(c => c.accountId))];
  for (const other of others) {
    try {
      const mine = await store.findOne({ accountId }).lean();
      if (!mine || mine.list.accepted.some(f => f.accountId === other)) continue;
      if (mine.list.blocked.some(f => f.accountId === other)) continue;
      const theirs = await store.findOne({ accountId: other }).lean();
      if (!theirs || theirs.list.blocked.some(f => f.accountId === accountId)) continue; // blocked either way: never auto-add
      await api.sendFriendReq(accountId, other);
      if (await api.acceptFriendReq(other, accountId) !== false) added.push(other);
    } catch (error) { console.warn('[Syndicate friends] could not auto-add', other, '-', error.message); }
  }
  return added;
}

// Presence bodies: a lobby party that is not in a match is always shown as joinable.
export function joinablePresence(body) {
  if (typeof body !== 'string' || !body.startsWith('{')) return body;
  try {
    const status = JSON.parse(body);
    if (!status || typeof status !== 'object' || status.bIsJoinable !== false) return body;
    const props = status.Properties || {};
    if (status.bIsPlaying || props.InUnjoinableMatch_b) return body;
    status.bIsJoinable = true;
    for (const key of Object.keys(props)) {
      const info = props[key];
      if (key.startsWith('party.joininfodata') && info && typeof info === 'object') { if ('bIsPrivate' in info) info.bIsPrivate = false; if ('notAcceptingReason' in info) info.notAcceptingReason = 0; }
    }
    return JSON.stringify(status);
  } catch { return body; }
}

// Join requests answered "party is private" (EPartyJoinDenialReason::PartyPrivate = 5) are accepted
// instead. A full party (2) or any other reason is left as it is.
export function joinableResponse(body) {
  if (typeof body !== 'string' || !body.includes('queryjoinability.response')) return body;
  try {
    const message = JSON.parse(body);
    const p = message?.payload;
    if (message.type !== 'com.epicgames.party.queryjoinability.response' || !p || p.isJoinable !== false || p.rejectionType !== 5) return body;
    p.isJoinable = true; p.rejectionType = 0;
    return JSON.stringify(message);
  } catch { return body; }
}
