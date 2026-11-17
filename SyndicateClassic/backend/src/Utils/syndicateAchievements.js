// Syndicate Achievements: the launcher's own long-term goals. They unlock in any season, are counted from the
// match events the game servers drop into the progression queue (syndicateProgression.js), and each pays V-Bucks
// into the account's shared wallet (Currency:MtxPurchased) when the player claims it in the launcher.
//
// Matches played with the Syndicate menu on never count: the launcher sets syndicate_menu_active at every launch
// (POST /launcher/menu) and recordAchievements() skips every event while it is set. The flag only changes at the
// next launch, so events from a menu match that are processed after the game closed still do not count.
//
// Everything lives in the Athena profile attributes (syndicate_ach), so it travels with the account like the career
// totals do.

// metric: the counter in syndicate_ach that is compared with target.
// Much harder since 2026-11-14 (Edwin: V-Bucks came too fast; kills against 99-bot lobbies pile up quickly). Ids
// are unchanged so claimed achievements stay claimed; a target that went up simply shows the new progress.
export const ACHIEVEMENTS = Object.freeze([
  {id:'first-blood',      name:'Bloodthirsty',      desc:'Eliminate 100 opponents.',                           metric:'kills',          target:100,   reward:100,   tier:'common',    badge:'crosshair'},
  {id:'first-victory',    name:'Victory Royale',    desc:'Win 10 matches.',                                    metric:'wins',           target:10,    reward:250,   tier:'uncommon',  badge:'crown'},
  {id:'regular',          name:'Battle Hardened',   desc:'Play 750 matches.',                                  metric:'matches',        target:750,   reward:600,   tier:'uncommon',  badge:'bus'},
  {id:'top-ten',          name:'Endgame Regular',   desc:'Finish in the top 10 in 250 matches.',               metric:'top10',          target:250,   reward:600,   tier:'rare',      badge:'storm'},
  {id:'sharpshooter',     name:'Sharpshooter',      desc:'Eliminate 5,000 opponents.',                         metric:'kills',          target:5000,  reward:800,   tier:'rare',      badge:'scope'},
  {id:'squad-wipe',       name:'Squad Wiper',       desc:'Get 25 eliminations in a single match.',             metric:'bestMatchKills', target:25,    reward:1000,  tier:'rare',      badge:'skull'},
  {id:'champion',         name:'Champion',          desc:'Win 150 matches.',                                   metric:'wins',           target:150,   reward:1200,  tier:'epic',      badge:'trophy'},
  {id:'time-traveller',   name:'Time Traveller',    desc:'Win a match in 10 different seasons.',               metric:'winSeasons',     target:10,    reward:1500,  tier:'epic',      badge:'rift'},
  {id:'island-veteran',   name:'Island Veteran',    desc:'Spend 500 hours in matches.',                        metric:'hours',          target:500,   reward:2000,  tier:'epic',      badge:'clock'},
  {id:'rampage',          name:'Rampage',           desc:'Get 40 eliminations in a single match.',             metric:'bestMatchKills', target:40,    reward:2500,  tier:'legendary', badge:'flame'},
  {id:'dominator',        name:'Dominator',         desc:'Win a match with 50 or more eliminations.',          metric:'bestWinKills',   target:50,    reward:3000,  tier:'legendary', badge:'lightning'},
  {id:'exterminator',     name:'Exterminator',      desc:'Eliminate 25,000 opponents.',                        metric:'kills',          target:25000, reward:3500,  tier:'legendary', badge:'target'},
  {id:'unstoppable',      name:'Unstoppable',       desc:'Win 750 matches.',                                   metric:'wins',           target:750,   reward:5000,  tier:'legendary', badge:'llama'},
  {id:'syndicate-legend', name:'Syndicate Legend',  desc:'Unlock every other achievement.',                    metric:'unlocked',       target:13,    reward:10000, tier:'mythic',    badge:'star'},
]);

const n = v => Math.max(0, Math.floor(Number(v) || 0));

function store(attr) {
  const s = attr.syndicate_ach ??= {};
  for (const k of ['kills','wins','matches','top10','seconds','bestMatchKills','bestWinKills'])s[k] = n(s[k]);
  if (!Array.isArray(s.winSeasons)) s.winSeasons = [];
  if (!Array.isArray(s.seasons)) s.seasons = [];
  if (!Array.isArray(s.claimed)) s.claimed = [];
  if (typeof s.match !== 'string') s.match = '';
  s.matchKills = n(s.matchKills);
  return s;
}

/** Counts one match event toward the achievements, unless the Syndicate menu was on. True when it counted. */
export function recordAchievements(attr, event) {
  if (!attr || !event || !['kill','win','match'].includes(event.kind)) return false;
  if (attr.syndicate_menu_active) return false;
  const s = store(attr);
  const match = String(event.match || '');
  // Eliminations in the current match: reset when a new match id shows up.
  if (match && s.match !== match) { s.match = match; s.matchKills = 0; }
  const season = n(event.season);
  if (season && !s.seasons.includes(season)) s.seasons.push(season);
  if (event.kind === 'kill') {
    s.kills++;
    if (match) { s.matchKills++; s.bestMatchKills = Math.max(s.bestMatchKills, s.matchKills); }
  } else if (event.kind === 'win') {
    s.wins++;
    if (season && !s.winSeasons.includes(season)) s.winSeasons.push(season);
    if (match && s.match === match) s.bestWinKills = Math.max(s.bestWinKills, s.matchKills);
  } else {
    s.matches++;
    s.seconds += Math.min(n(event.seconds), 4 * 3600);
    const place = n(event.place);
    if (place >= 1 && place <= 10) s.top10++;
  }
  return true;
}

function value(def, s, unlockedOthers) {
  switch (def.metric) {
    case 'hours': return Math.floor(s.seconds / 360) / 10; // one decimal
    case 'winSeasons': return s.winSeasons.length;
    case 'unlocked': return unlockedOthers;
    default: return n(s[def.metric]);
  }
}

/** Every achievement with the player's progress, for the launcher (Home and the Achievements page). */
export function achievementView(attr) {
  const s = store(attr || {});
  const plain = ACHIEVEMENTS.filter(a => a.metric !== 'unlocked');
  const unlockedOthers = plain.filter(a => value(a, s, 0) >= a.target).length;
  const list = ACHIEVEMENTS.map(a => {
    const progress = value(a, s, unlockedOthers);
    const unlocked = progress >= a.target;
    return {...a, progress: Math.min(progress, a.target), unlocked, claimed: s.claimed.includes(a.id)};
  });
  return {
    achievements: list,
    menuActive: !!attr?.syndicate_menu_active,
    totals: {kills: s.kills, wins: s.wins, matches: s.matches, top10: s.top10, hours: Math.floor(s.seconds / 360) / 10,
             seasons: s.seasons.length, bestMatchKills: s.bestMatchKills},
    rewardTotal: ACHIEVEMENTS.reduce((t, a) => t + a.reward, 0),
    rewardClaimed: ACHIEVEMENTS.filter(a => s.claimed.includes(a.id)).reduce((t, a) => t + a.reward, 0),
  };
}

/**
 * Claims one unlocked achievement: marks it claimed in the Athena attributes and adds its reward to the shared
 * V-Bucks wallet in common_core. The caller saves both profiles. Throws a player-readable message when it can't.
 */
export function claimAchievement(profiles, id, walletId, ensureWallet) {
  const attr = profiles?.athena?.stats?.attributes;
  if (!attr) throw new Error('Play a match first.');
  const view = achievementView(attr);
  const a = view.achievements.find(x => x.id === id);
  if (!a) throw new Error('Unknown achievement.');
  if (!a.unlocked) throw new Error('Not unlocked yet.');
  if (a.claimed) throw new Error('Already claimed.');
  ensureWallet(profiles.common_core);
  const wallet = profiles.common_core.items[walletId];
  wallet.quantity += a.reward;
  store(attr).claimed.push(id);
  return {reward: a.reward, balance: wallet.quantity};
}
