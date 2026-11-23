// Syndicate: Save the World, dev launcher only (TILTED_DEVKIT=1).
//
// Getting into Save the World on Pre-season 1.7.2 as a brand-new player, tutorial included. What the client needs:
//  - Save the World access. Pre-season and Season 1 (1.7.2 to 1.11) keep Save the World in profile0, not
//    campaign: the 1.7.2 log shows it querying profile0, metadata, theater0, outpost0 and the collection books,
//    never campaign or common_core, and showing the Founder's Pack screen until profile0 held
//    Token:campaignaccess. Later builds read the same token from common_core. Both get it (and the Founder's
//    pack token).
//  - A fresh Save the World account: no heroes, schematics, Homebase upgrades or quests yet, level 1, no
//    Homebase name. A new player starts with the tutorial (a mission, which needs a Save the World game server).
//    An earlier version of this gave LawinServer's maxed-out profile (everything unlocked, tutorial done); an
//    account set up that way is reset once to the fresh one (STW_DEV_VERSION). Version 3 adds the first quest,
//    Quest:homebaseonboarding, which starts the tutorial.
//  - /fortnite/api/game/v2/world/info: the theaters and missions of the Save the World map (LawinServer's
//    worldstw.json; it was {}).
//  - Save the World profile operations without a handler here must not answer 404 (each one put an error
//    window up).
//  - Version 4: a starting kit. With no hero the zone had no one to dress the player as (the log's "Hero: ") and
//    the pawn was invisible, with no pickaxe or weapons. A new player now has one Soldier hero as commander
//    (Squad_Combat_AdventureSquadOne slot 0, as in LawinServer's profile), the T00 starter schematics, and a
//    backpack (theater0) with the edit tool, the build pieces, the three materials, ammo and a starter pistol and
//    rifle.
//  - Version 5: no starting materials (the tutorial starts with none and has the player harvest them).
// The version marker lives in profile0's stats (syndicate_stw_dev), since every account has a profile0.
//
// Season 3 and later keep Save the World in the campaign profile, which used to stay LawinServer's empty template:
// no tutorial, no quests, heroes or schematics, so every tab was empty. Now (campaign stat syndicate_stw_campaign):
//  - 'tutorial': a new player's campaign, the same start as profile0 (the tutorial quest, the starting hero as
//    commander, the starter schematics). The client plays the tutorial mission first.
//  - 'unlocked': after the tutorial, every Save the World profile becomes LawinServer's full one (every hero,
//    survivor, schematic, research level, upgrade and quest), Pre-season's profile0 included. The account keeps its
//    wallet, banners and Homebase name. The tutorial counts as done when its quest says so (the mission or the
//    Homebase naming) or, since a mission may not report back, once the player has been in the tutorial mission
//    (matchmaking.js stwTutorialEntered) and is back in the lobby a few minutes later.
import fs from 'fs';
import path from 'path';
import { createRequire } from 'module';
import { dirname } from 'dirname-filename-esm';
import { stwEnabledFor } from './syndicateSeasonModes.js';

const require = createRequire(import.meta.url);
const { lawinProfile } = require('./syndicateStwLawin.cjs');
const __dirname = dirname(import.meta);
const stwFolder = path.join(__dirname, '../Profiles/StwDev');
const defaults = path.join(__dirname, '../Profiles/DefaultProfiles');

export const STW_DEV_VERSION = 6;
export const STW_PROFILES = Object.freeze(['campaign', 'theater0', 'outpost0', 'metadata', 'collection_book_people0',
  'collection_book_schematics0', 'collections', 'common_public']);
const ACCESS_TOKENS = Object.freeze(['Token:campaignaccess', 'Token:founderspack_1']);
const KEEP = /^(Currency|HomebaseBannerColor|HomebaseBannerIcon):/i; // the account's own wallet and banners

// The Dev Kit (TILTED_DEVKIT=1, every build, as before) or, with the client's user agent, the launcher's Save the World
// switch for that season (Settings > Game modes, syndicateSeasonModes.js). Experimental outside the Dev Kit.
export const stwDevEnabled = (userAgent) => stwEnabledFor(userAgent);

const worldInfo = new Map();
// The Save the World map for one season: Stonewood to Twine Peaks plus, as LawinServer does, that season's event
// zone (worldstw.json "Seasonal": Season 8's Spring event, Season 10's Hit the Road...), which the season's event
// quests send the player to.
export function stwWorldInfo(season) {
  const key = Number(season) || 0;
  let world = worldInfo.get(key);
  if (!world) {
    world = JSON.parse(fs.readFileSync(path.join(stwFolder, 'worldstw.json'), 'utf8'));
    const seasonal = world.Seasonal?.['Season' + key];
    if (seasonal) {
      world.theaters = world.theaters.concat(seasonal.theaters || []);
      world.missions = world.missions.concat(seasonal.missions || []);
      world.missionAlerts = world.missionAlerts.concat(seasonal.missionAlerts || []);
    }
    delete world.Seasonal;
    worldInfo.set(key, world);
  }
  // LawinServer's data is from 2017: every mission and alert "availableUntil" has long passed, so the game treats
  // them (the tutorial's onboarding mission included) as gone ("ZoneInfo Invalid!"). Keep them a day ahead.
  const until = new Date(Date.now() + 86400000).toISOString();
  for (const m of world.missions || []) { m.nextRefresh = until; for (const a of m.availableMissions || []) a.availableUntil = until; }
  for (const m of world.missionAlerts || []) { m.nextRefresh = until; for (const a of m.availableMissionAlerts || []) a.availableUntil = until; }
  return world;
}

const template = (profileId) => JSON.parse(fs.readFileSync(path.join(defaults, `${profileId}.json`), 'utf8'));

function stamp(fresh, old, profileId, accountId) {
  fresh.profileId = profileId;
  fresh.accountId = accountId;
  fresh.created = old?.created || new Date().toISOString();
  fresh.updated = new Date().toISOString();
  // Keep the revision moving forward so the client never sees an older profile.
  fresh.rvn = Math.max(Number(old?.rvn) || 0, Number(fresh.rvn) || 0) + 1;
  fresh.commandRevision = Math.max(Number(old?.commandRevision) || 0, Number(fresh.commandRevision) || 0) + 1;
  return fresh;
}

const tokenItem = (templateId) => ({ templateId, attributes: { max_level_bonus: 0, level: 1, item_seen: true, xp: 0, favorite: false }, quantity: 1 });
const tokenKey = (templateId) => `syndicate-stw-${templateId.split(':')[1].toLowerCase()}`;

const STARTER_HERO = 'HID_Commando_GrenadeGun_UC_T03';
const STARTER_WEAPONS = Object.freeze(['WID_Pistol_SemiAuto_C_Ore_T00', 'WID_Assault_Auto_C_Ore_T00']);
const STARTER_AMMO = Object.freeze(['AmmoDataBulletsLight', 'AmmoDataBulletsMedium', 'AmmoDataShells', 'AmmoDataBulletsHeavy',
  'AmmoDataEnergyCell']); // AmmoDataExplosive is not an item in 1.7.2 ("did not parse correctly")
// A new player's backpack.
function freshTheater0(old, accountId) {
  const fresh = template('theater0');
  const tool = { clipSizeScale: 0, loadedAmmo: 0, inventory_overflow_date: false, level: 0, alterationDefinitions: [],
    baseClipSize: 0, durability: 1, itemSource: '' };
  const items = {};
  for (const id of ['EditTool', 'BuildingItemData_Wall', 'BuildingItemData_Floor', 'BuildingItemData_Stair_W', 'BuildingItemData_RoofS'])
    items[`syndicate-stw-${id.toLowerCase()}`] = { templateId: `Weapon:${id}`, attributes: { ...tool }, quantity: 1 };
  for (const id of STARTER_AMMO)
    items[`syndicate-stw-${id.toLowerCase()}`] = { templateId: `Ammo:${id}`, attributes: { level: 0, durability: 1 }, quantity: 120 };
  for (const id of STARTER_WEAPONS)
    items[`syndicate-stw-${id.toLowerCase()}`] = { templateId: `Weapon:${id}`, attributes: { ...tool, level: 1, loadedAmmo: 0 }, quantity: 1 };
  fresh.items = items;
  const slots = n => Array.from({ length: n }, () => ({ items: [] }));
  const primary = slots(9);
  STARTER_WEAPONS.forEach((id, i) => { primary[i + 1].items = [`syndicate-stw-${id.toLowerCase()}`]; });
  fresh.stats.attributes = { ...fresh.stats.attributes,
    player_loadout: { bPlayerIsNew: true, pinnedSchematicInstances: [], primaryQuickBarRecord: { slots: primary },
      secondaryQuickBarRecord: { slots: slots(7) }, zonesCompleted: 0 },
    theater_unique_id: '', past_lifetime_zones_completed: 0, last_event_instance_key: '', last_zones_completed: 0,
    // The backpack's size (it showed 0, which also greyed out crafting: a crafted item has nowhere to go).
    inventory_limit_bonus: 100 };
  return stamp(fresh, old, 'theater0', accountId);
}

// What a new player starts with, in profile0 (Pre-season to Season 2) and campaign (Season 3 and later).
function starterKit() {
  const items = {};
  // A new player's first quest. Epic's server handed it out at the first ClientQuestLogin; without it the 1.7.2
  // lobby knew a tutorial was needed ("Tutorial needed" in the log) but had no quest to play. Its objectives are
  // the tutorial mission (hbonboarding_completezone), the satellite cinematic and naming the Homebase.
  items['syndicate-stw-homebaseonboarding'] = { templateId: 'Quest:homebaseonboarding', attributes: {
    quest_state: 'Active', last_state_change_time: new Date().toISOString(), max_level_bonus: 0, level: -1, xp: 0,
    completion_hbonboarding_completezone: 0, completion_hbonboarding_watchsatellitecine: 0, completion_hbonboarding_namehomebase: 0,
    item_seen: false, sent_new_notification: false, favorite: false }, quantity: 1 };
  // The starting hero, the commander (the zone dresses the player as the commander).
  items['syndicate-stw-starter-hero'] = { templateId: `Hero:${STARTER_HERO}`, attributes: {
    equipped_cosmetics: ['', '', '', '', '', '', '', ''], gender: 0, level: 1, item_seen: true,
    squad_slot_idx: 0, portrait: '', hero_name: 'DefaultHeroName', max_level_bonus: 0,
    squad_id: 'Squad_Combat_AdventureSquadOne', mode_loadouts: [], xp: 0, slotted_building_id: '',
    building_slot_used: -1, favorite: false, outfitvariants: [], backblingvariants: [], refundable: false }, quantity: 1 };
  for (const id of STARTER_WEAPONS)
    items[`syndicate-stw-${id.toLowerCase()}`] = { templateId: `Schematic:${id.replace(/^WID_/, 'SID_')}`, attributes: {
      level: 1, item_seen: true, xp: 0, max_level_bonus: 0, favorite: false, alterations: [], refundable: false }, quantity: 1 };
  return items;
}

const ONBOARDING = 'quest:homebaseonboarding';
const onboardingQuest = (profile) => Object.values(profile?.items || {}).find(item => String(item?.templateId || '').toLowerCase() === ONBOARDING);
// The tutorial is done once its quest is (Claimed/Completed) or its mission was (hbonboarding_completezone).
export function stwTutorialDone(profile) {
  const quest = onboardingQuest(profile);
  if (!quest?.attributes) return false;
  return /^(claimed|completed)$/i.test(String(quest.attributes.quest_state || '')) || Number(quest.attributes.completion_hbonboarding_completezone) > 0;
}
// Every objective of the tutorial quest done and the quest claimed, so no build sends the player back into it.
function finishOnboarding(profile) {
  for (const item of Object.values(profile?.items || {})) {
    if (String(item?.templateId || '').toLowerCase() !== ONBOARDING || !item.attributes) continue;
    for (const key of Object.keys(item.attributes)) if (key.toLowerCase().startsWith('completion_')) item.attributes[key] = 1;
    item.attributes.quest_state = 'Claimed';
    item.attributes.last_state_change_time = new Date().toISOString();
  }
}

// Season 3 and later: a new player's campaign, the tutorial start (syndicate_stw_campaign 'tutorial').
function freshCampaign(old, accountId) {
  const fresh = template('campaign');
  const items = starterKit();
  // The hero loadout (later builds pick the commander from it; earlier ones from the hero's squad, set above).
  items['syndicate-stw-loadout1'] = { templateId: 'CampaignHeroLoadout:defaultloadout', attributes: { team_perk: '', loadout_name: '',
    crew_members: { commanderslot: 'syndicate-stw-starter-hero', followerslot1: '', followerslot2: '', followerslot3: '',
      followerslot4: '', followerslot5: '' }, loadout_index: 0, gadgets: [{ gadget: '', slot_index: 0 }, { gadget: '', slot_index: 1 }] }, quantity: 1 };
  // The Save the World cosmetic locker (pickaxe, back bling, emote and music in missions), LawinServer's default.
  try {
    const full = lawinProfile('campaign');
    for (const [key, item] of Object.entries(full.items || {}))
      if (/^CosmeticLocker:cosmeticlocker_stw$/i.test(item?.templateId || '') && key === 'CosmeticLocker:cosmeticlocker_stw') {
        item.attributes.locker_name = '';
        items[key] = item;
      }
  } catch (error) { console.log('[Syndicate STW] no Save the World locker for the tutorial start:', error.message); }
  fresh.items = items;
  fresh.stats.attributes = {
    level: 1, xp: 0, xp_overflow: 0, packs_granted: 0, matches_played: 0, rewards_claimed_post_max_level: 0,
    node_costs: {}, research_levels: { technology: 0, fortitude: 0, offense: 0, resistance: 0 }, legacy_research_points_spent: 0,
    selected_hero_loadout: 'syndicate-stw-loadout1', use_random_loadout: false, mode_loadouts: [],
    loadouts: items['CosmeticLocker:cosmeticlocker_stw'] ? ['CosmeticLocker:cosmeticlocker_stw'] : [],
    last_applied_loadout: items['CosmeticLocker:cosmeticlocker_stw'] ? 'CosmeticLocker:cosmeticlocker_stw' : '', active_loadout_index: 0,
    collection_book: { pages: [], maxBookXpLevelAchieved: 0 }, mission_alert_redemption_record: { claimData: [] },
    quest_manager: { dailyLoginInterval: '0001-01-01T00:00:00.000Z', dailyQuestRerolls: 1 },
    daily_rewards: { nextDefaultReward: 0, totalDaysLoggedIn: 0, lastClaimDate: '0001-01-01T00:00:00.000Z', additionalSchedules: {} },
    gameplay_stats: [], client_settings: { pinnedQuestInstances: [] }, permissions: [], twitch: {}, unslot_mtx_spend: 0,
    inventory_limit_bonus: 0, mfa_reward_claimed: false, xp_lost: 0,
    syndicate_stw_campaign: 'tutorial',
  };
  return stamp(fresh, old, 'campaign', accountId);
}

// LawinServer's full profile, ready to save for this account (its own revision kept moving forward).
function fullProfile(id, old, accountId) {
  const full = lawinProfile(id);
  delete full._id;
  return stamp(full, old, id, accountId);
}

// After the tutorial: every Save the World profile LawinServer's full one (see the top of this file).
function unlockedProfiles(doc, set) {
  const accountId = doc.accountId;
  const current = (id) => set[`profiles.${id}`] || doc.profiles?.[id];
  const campaign = fullProfile('campaign', current('campaign'), accountId);
  finishOnboarding(campaign);
  campaign.stats.attributes.syndicate_stw_campaign = 'unlocked';
  // Daily quests start from the next ClientQuestLogin; the time is LawinServer's from 2020.
  if (campaign.stats.attributes.quest_manager) campaign.stats.attributes.quest_manager.dailyLoginInterval = '0001-01-01T00:00:00.000Z';
  set['profiles.campaign'] = campaign;

  // Pre-season to Season 2 (profile0): the full 2017 profile with the account's own wallet, banners and Homebase.
  const old0 = current('profile0');
  const p0 = fullProfile('profile0', old0, accountId);
  const items = {};
  for (const [key, item] of Object.entries(p0.items || {})) if (!KEEP.test(item?.templateId || '')) items[key] = item;
  for (const [key, item] of Object.entries(old0?.items || {})) if (KEEP.test(item?.templateId || '')) items[key] = item;
  const have = new Set(Object.values(items).map(item => item?.templateId));
  for (const token of ACCESS_TOKENS) if (!have.has(token)) items[tokenKey(token)] = tokenItem(token);
  p0.items = items;
  finishOnboarding(p0);
  const a = p0.stats.attributes, oa = old0?.stats?.attributes || {};
  if (oa.homebase) a.homebase = oa.homebase;
  for (const key of ['current_mtx_platform', 'in_app_purchases', 'daily_purchases', 'weekly_purchases', 'monthly_purchases', 'mtx_affiliate'])
    if (key in oa) a[key] = oa[key];
  a.syndicate_stw_dev = STW_DEV_VERSION;
  set['profiles.profile0'] = p0;

  // The storm shields (metadata) and the collection book.
  for (const id of ['metadata', 'collection_book_people0', 'collection_book_schematics0'])
    set[`profiles.${id}`] = fullProfile(id, current(id), accountId);
}

// How long after going into the tutorial mission a lobby profile request counts as being back from it.
const TUTORIAL_BACK_MS = 5 * 60 * 1000;

// A new player's profile0: the account's wallet and banners, Save the World access, nothing else.
function freshProfile0(old, accountId) {
  const fresh = template('profile0');
  const items = {};
  for (const [key, item] of Object.entries(old?.items || fresh.items || {}))
    if (KEEP.test(item?.templateId || '')) items[key] = item;
  for (const token of ACCESS_TOKENS) items[tokenKey(token)] = tokenItem(token);
  Object.assign(items, starterKit());
  fresh.items = items;
  const a = fresh.stats.attributes;
  Object.assign(a, {
    level: 1, xp: 0, packs_granted: 0, node_costs: {}, gameplay_stats: [], default_hero_squad_id: '',
    collection_book: { pages: [], maxBookXpLevelAchieved: 0 },
    quest_manager: { dailyLoginInterval: '0001-01-01T00:00:00.000Z', dailyQuestRerolls: 1 },
    named_counters: { SubGameSelectCount_Campaign: { current_count: 0, last_incremented_time: '' },
      SubGameSelectCount_Athena: { current_count: 0, last_incremented_time: '' } },
    homebase: { townName: '', bannerIconId: 'StandardBanner1', bannerColorId: 'DefaultColor1', flagPattern: -1, flagColor: -1 },
    syndicate_stw_dev: STW_DEV_VERSION,
  });
  // Purchase records and the wallet platform stay the account's own.
  for (const key of ['current_mtx_platform', 'in_app_purchases', 'daily_purchases', 'weekly_purchases', 'monthly_purchases', 'mtx_affiliate'])
    if (old?.stats?.attributes && key in old.stats.attributes) a[key] = old.stats.attributes[key];
  return stamp(fresh, old, 'profile0', accountId);
}

// Returns the $set update for the account's profiles document, or null when nothing needs to change.
// request: { operation, profileId } of the profile request it runs for (the "back from the tutorial" check).
export function stwDevUpdate(doc, userAgent, request = {}) {
  if (!stwDevEnabled(userAgent) || !doc?.profiles) return null;
  const set = {};
  const old0 = doc.profiles.profile0;
  if (old0 && (Number(old0.stats?.attributes?.syndicate_stw_dev) || 0) < STW_DEV_VERSION) {
    set['profiles.profile0'] = freshProfile0(old0, doc.accountId);
    for (const id of STW_PROFILES)
      set[`profiles.${id}`] = id === 'theater0' ? freshTheater0(doc.profiles[id], doc.accountId)
        : id === 'campaign' ? freshCampaign(doc.profiles[id], doc.accountId)
        : stamp(template(id), doc.profiles[id], id, doc.accountId);
  }
  // Season 3 and later: the tutorial start, then everything unlocked (see the top of this file).
  try {
    const campaign = set['profiles.campaign'] || doc.profiles.campaign;
    const p0 = set['profiles.profile0'] || old0;
    const state = campaign?.stats?.attributes?.syndicate_stw_campaign;
    const entered = Number(p0?.stats?.attributes?.syndicate_stw_tutorial_entered) || 0;
    const back = entered > 0 && Date.now() - entered >= TUTORIAL_BACK_MS && ['campaign', 'profile0'].includes(request.profileId)
      && ['QueryProfile', 'ClientQuestLogin'].includes(request.operation);
    if (state !== 'unlocked' && (stwTutorialDone(campaign) || stwTutorialDone(p0) || back)) {
      unlockedProfiles(doc, set);
      console.log(`[Syndicate STW] tutorial done (${back ? 'back from the tutorial mission' : 'tutorial quest complete'}): `
        + `every Save the World profile unlocked for ${doc.accountId}`);
    } else if (campaign && !state) {
      set['profiles.campaign'] = freshCampaign(campaign, doc.accountId);
      console.log('[Syndicate STW] Save the World tutorial start set up for', doc.accountId);
    }
  } catch (error) {
    console.error('[Syndicate STW] tutorial/unlock setup skipped:', error.message);
  }
  // The Homebase banner must be one the account owns: the default profile points at OT11Banner, which the banner
  // cleanup removes, and the Homebase banner picker then showed nothing and could not Apply.
  const p0 = set['profiles.profile0'] || old0;
  const home = p0?.stats?.attributes?.homebase;
  if (home) {
    const owned = new Set(Object.values(p0.items || {}).map(item => String(item?.templateId || '').toLowerCase()));
    const icon = `homebasebannericon:${String(home.bannerIconId || '').toLowerCase()}`;
    const color = `homebasebannercolor:${String(home.bannerColorId || '').toLowerCase()}`;
    if (!owned.has(icon) || !owned.has(color)) {
      const firstColor = Object.values(p0.items || {}).find(item => /^HomebaseBannerColor:/i.test(item?.templateId || ''));
      const fixed = { ...home,
        bannerIconId: owned.has(icon) ? home.bannerIconId : 'StandardBanner1',
        bannerColorId: owned.has(color) ? home.bannerColorId : (firstColor ? firstColor.templateId.split(':')[1] : 'DefaultColor1') };
      if (set['profiles.profile0']) set['profiles.profile0'].stats.attributes.homebase = fixed;
      else {
        set['profiles.profile0.stats.attributes.homebase'] = fixed;
        set['profiles.profile0.rvn'] = (Number(old0.rvn) || 0) + 1;
        set['profiles.profile0.commandRevision'] = (Number(old0.commandRevision) || 0) + 1;
      }
    }
  }
  const core = doc.profiles.common_core;
  if (core?.items) {
    const have = new Set(Object.values(core.items).map(item => item?.templateId));
    for (const token of ACCESS_TOKENS)
      if (!have.has(token)) set[`profiles.common_core.items.${tokenKey(token)}`] = tokenItem(token);
    if (Object.keys(set).some(key => key.startsWith('profiles.common_core.')))
      set['profiles.common_core.rvn'] = (Number(core.rvn) || 0) + 1;
  }
  return Object.keys(set).length ? set : null;
}

// The player is going into a Save the World mission (matchmaking.js). Before the tutorial is done that mission is
// the tutorial: returns the $set that records when, or null.
export function stwTutorialEntry(doc) {
  const campaign = doc?.profiles?.campaign, p0 = doc?.profiles?.profile0;
  if (!p0 || campaign?.stats?.attributes?.syndicate_stw_campaign === 'unlocked' || stwTutorialDone(campaign) || stwTutorialDone(p0)) return null;
  return { 'profiles.profile0.stats.attributes.syndicate_stw_tutorial_entered': Date.now() };
}

// Where the account is: 'tutorial', 'unlocked', or '' (not set up yet).
export const stwCampaignState = (doc) => doc?.profiles?.campaign?.stats?.attributes?.syndicate_stw_campaign || '';

// Profile operations the Save the World lobby sends that have no handler here: answer with no changes.
export function stwDevNoOp(profileId, userAgent) {
  return stwDevEnabled(userAgent) && profileId !== 'athena' && profileId !== 'common_core';
}
