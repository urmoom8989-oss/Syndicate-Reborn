// Syndicate: the launcher's "Unlock all" also maxes the season. While stats.attributes.tilted_unlock_all is
// on, each season the player plays is held at its last level and Battle Pass tier (syndicateSeasonState.js
// holdSeasonMax), and every challenge it has is complete. Before a season is first maxed, its real level,
// pass and challenge state (the same capture a season switch keeps) is saved in tilted_maxed_before[season];
// turning Unlock all off puts each saved season back exactly. No tier or challenge reward is claimed for
// the maxed state (Unlock all already gives the cosmetics), so nothing is left behind after the restore.
import fs from 'node:fs';
import {activeSeason,captureSeason,putSeason,holdSeasonMax} from './syndicateSeasonState.js';
import {challengeSeasons,loadChallengeData,grantSeasonQuests} from './syndicateSeasonChallenges.js';
import {definitions as season4Quests,ensureChallenges} from './syndicateChallenges.js';
import {ensureSeason5Bundles} from './syndicateSeason5Bundles.js';

const readJson = name => JSON.parse(fs.readFileSync(new URL(name, import.meta.url), 'utf8'));
const season4Bundles = readJson('./syndicateChallengeBundles.json'), season5Bundles = readJson('./syndicateSeason5Bundles.json');
const clone = x => JSON.parse(JSON.stringify(x));

// Quest id (lower case) -> objectives [{name, count}] for one season, from every challenge source it uses.
function questTargets(season) {
  const map = new Map(), add = (id, objectives, claimed = false) => map.set(String(id).toLowerCase(), {objectives, claimed});
  for (const q of loadChallengeData(season)?.questMap?.values() || []) add(q.id, (q.objectives || []).map(o => ({name: o.name, count: Number(o.count) || 0})));
  const bundles = season === 4 ? season4Bundles : season === 5 ? season5Bundles : null;
  for (const b of bundles?.bundles || []) for (const q of b.quests || []) add(q.id, (q.objectives || []).map(o => ({name: o.name, count: Number(o.target) || 0})));
  // Season 4's own challenges (syndicateChallenges.js) end as Claimed, like progressChallenges leaves them.
  if (season === 4) for (const d of season4Quests) add(d.id, [{name: d.objective, count: Number(d.target) || 0}], true);
  return map;
}
// Every active quest with known objectives: objectives at their count, quest complete. tilted_rewarded keeps
// syndicateSeasonChallenges.js from handing out the rewards of these later. Returns how many were completed.
function completeQuests(profile, targets) {
  let completed = 0; const now = new Date().toISOString();
  for (const item of Object.values(profile.items || {})) {
    if (!/^quest:/i.test(item?.templateId || '')) continue;
    const a = item.attributes ??= {}; if (a.quest_state !== 'Active') continue;
    const target = targets.get(item.templateId.slice(6).toLowerCase()); if (!target?.objectives.length) continue;
    for (const o of target.objectives) a['completion_' + o.name] = Math.max(Number(a['completion_' + o.name]) || 0, o.count);
    a.quest_state = target.claimed ? 'Claimed' : 'Completed'; a.last_state_change_time = now;
    a.tilted_rewarded = 2; a.tilted_reward_claimed = true; completed++;
  }
  return completed;
}
// The season's next challenge stages and level-gated weeks, granted without their rewards.
function grantNext(profile, season) {
  if (challengeSeasons.has(season)) grantSeasonQuests(profile, season);
  else if (season === 4) ensureChallenges(profile);
  else if (season === 5) ensureSeason5Bundles(profile);
}

// Maxes the active season while Unlock all is on. True when the profile changed.
export function applyMaxProgress(profile, season) {
  const a = profile?.stats?.attributes;
  if (!a?.tilted_unlock_all || !Number.isInteger(season) || activeSeason(profile) !== season) return false;
  const before = JSON.stringify(captureSeason(profile));
  const saved = a.tilted_maxed_before && typeof a.tilted_maxed_before === 'object' ? a.tilted_maxed_before : (a.tilted_maxed_before = {});
  const first = !saved[season];
  if (first) saved[season] = captureSeason(profile);
  holdSeasonMax(profile, season);
  // Completing a stage or reaching the last tier unlocks more quests: repeat until none are left.
  const targets = questTargets(season);
  const questCount = () => Object.values(profile.items || {}).filter(i => /^quest:/i.test(i?.templateId || '')).length;
  for (let pass = 0; pass < 16; pass++) {
    const n = completeQuests(profile, targets), had = questCount();
    grantNext(profile, season);
    if (!n && questCount() === had) break;
  }
  holdSeasonMax(profile, season);
  return first || before !== JSON.stringify(captureSeason(profile));
}
// True while the season is held at max (syndicateProgressionCore.js then leaves its level alone).
export function seasonMaxed(attributes, season) {
  return !!attributes?.tilted_unlock_all && !!attributes?.tilted_maxed_before?.[season];
}
// Puts every maxed season back to its saved state. Returns how many seasons were restored.
export function restoreMaxProgress(profile) {
  const a = profile?.stats?.attributes, saved = a?.tilted_maxed_before;
  if (!a || saved === undefined) return 0;
  delete a.tilted_maxed_before;
  if (!saved || typeof saved !== 'object') return 0;
  const active = activeSeason(profile); let restored = 0;
  for (const [season, state] of Object.entries(saved)) {
    if (!state?.attributes) continue;
    if (Number(season) === active) putSeason(profile, state);
    else (a.tilted_season_states ??= {})[season] = clone(state);
    restored++;
  }
  return restored;
}
