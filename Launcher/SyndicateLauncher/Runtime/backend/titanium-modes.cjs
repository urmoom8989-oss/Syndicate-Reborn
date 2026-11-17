"use strict";

const fs = require("fs");
const path = require("path");

const root = path.resolve(process.cwd());
const profilesRoot = path.join(root, "profiles");
const ownerPath = path.join(root, "profile-owner.json");
const bpCache = new Map();

// Classic daily challenges have no count in quests.json, so their real targets
// live here. Without these, the first point of progress was treated as a finish.
const DAILY_OBJECTIVE_TARGETS = Object.freeze({
    daily_athena_outlive_solo_players: 150,
    daily_athena_outlive_squad_players_v2: 150,
    daily_athena_outlive_players_v3: 300,
    daily_athena_play_matches_v3: 7,
    daily_athena_solo_top25_v2: 1,
    daily_athena_squad_top6_v2: 1,
    athena_daily_loot_ammobox_v2: 7,
    daily_athena_loot_chest_v2: 7,
    athena_daily_kill_players_v2: 10,
    athena_daily_kill_players_assault_rifles: 5,
    athena_daily_kill_players_pistol_v3: 3,
    athena_daily_kill_players_shotgun_v2: 4,
    athena_daily_kill_players_smg_v2: 3,
    athena_daily_kill_players_sniper_v2: 2
});

function normalizeObjective(value) {
    return String(value || "").trim().toLowerCase()
        .replace(/^completion_/, "")
        .replace(/_v\d+$/, "");
}

function objectiveTokens(value) {
    return new Set(normalizeObjective(value).split("_").filter(token =>
        token && !["athena", "daily", "quest", "br", "completion"].includes(token)
    ).map(token => token.length > 4 && token.endsWith("s") && !token.endsWith("ss") ? token.slice(0, -1) : token));
}

function objectivesMatch(left, right) {
    const a = normalizeObjective(left);
    const b = normalizeObjective(right);
    if (!a || !b) return false;
    if (a === b) return true;
    const leftTokens = objectiveTokens(a);
    const rightTokens = objectiveTokens(b);
    if (!leftTokens.size || leftTokens.size !== rightTokens.size) return false;
    for (const token of leftTokens) if (!rightTokens.has(token)) return false;
    return true;
}

function readOwner() {
    try {
        return JSON.parse(fs.readFileSync(ownerPath, "utf8"));
    } catch (_) {
        return {};
    }
}

function seasonNumber(memory) {
    const fromBuild = Number(memory && memory.season);
    if (Number.isFinite(fromBuild) && fromBuild > 0) return fromBuild;
    const fromOwner = Number(readOwner().season);
    return Number.isFinite(fromOwner) && fromOwner > 0 ? fromOwner : 10;
}

function profileMode(profile) {
    const marker = profile.stats && profile.stats.attributes && profile.stats.attributes.titanium_mode;
    if (marker === "progression" || marker === "unlocked-all") return marker;
    return readOwner().mode === "progression" ? "progression" : "unlocked-all";
}

function save(name, profile) {
    fs.writeFileSync(path.join(profilesRoot, name + ".json"), JSON.stringify(profile, null, 2));
}

function bump(profile) {
    profile.rvn = (profile.rvn || 0) + 1;
    profile.commandRevision = (profile.commandRevision || 0) + 1;
}

function loadBattlePass(season) {
    if (bpCache.has(season)) return bpCache.get(season);
    let value = null;
    try {
        value = JSON.parse(fs.readFileSync(path.join(root, "responses", "Athena", "BattlePass", "Season" + season + ".json"), "utf8"));
    } catch (_) {
        value = null;
    }
    bpCache.set(season, value);
    return value;
}

function findCurrency(core) {
    for (const key of Object.keys(core.items || {})) {
        const item = core.items[key];
        if (String(item.templateId || "").toLowerCase().startsWith("currency:mtx")) return { key, item };
    }
    return null;
}

function resetCurrency() {
    const core = global.tpf("common_core");
    core.items = core.items || {};
    let currency = findCurrency(core);
    if (!currency) {
        core.items.Currency = {
            templateId: "Currency:MtxPurchased",
            attributes: { platform: "EpicPC" },
            quantity: 0
        };
        currency = { key: "Currency", item: core.items.Currency };
    }
    currency.item.quantity = 0;
    bump(core);
    save("common_core", core);
}

function addCurrency(amount) {
    const core = global.tpf("common_core");
    core.items = core.items || {};
    let currency = findCurrency(core);
    if (!currency) {
        core.items.Currency = {
            templateId: "Currency:MtxPurchased",
            attributes: { platform: "EpicPC" },
            quantity: 0
        };
        currency = { key: "Currency", item: core.items.Currency };
    }
    currency.item.quantity = Math.max(0, Number(currency.item.quantity || 0) + Number(amount || 0));
    bump(core);
    save("common_core", core);
    return currency.key;
}

function isCosmetic(templateId) {
    return /^(AthenaCharacter|AthenaBackpack|AthenaPickaxe|AthenaGlider|AthenaSkyDiveContrail|AthenaDance|AthenaItemWrap|AthenaMusicPack|AthenaLoadingScreen|AthenaPet|AthenaPetCarrier|AthenaToy|AthenaEmoji|AthenaSpray|AthenaBattleBus|AthenaVehicleCosmetic|AthenaRewardEventGraphCosmetic|CosmeticVariantToken):/i.test(templateId);
}

function isChallengeItem(templateId) {
    return /^(Quest|ChallengeBundle|ChallengeBundleSchedule):/i.test(templateId);
}

function isDailyQuest(templateId) {
    return /^Quest:AthenaDaily/i.test(templateId);
}

function purgeForeignChallenges(profile, season, keepDaily) {
    let changed = false;
    for (const key of Object.keys(profile.items || {})) {
        const templateId = String(profile.items[key].templateId || "");
        if (!isChallengeItem(templateId)) continue;
        if (key.startsWith("S" + season + "-")) continue;
        if (keepDaily && isDailyQuest(templateId)) continue;
        delete profile.items[key];
        changed = true;
    }
    return changed;
}

function isStarterCosmetic(templateId) {
    return /default/i.test(templateId)
        || /^AthenaCharacter:CID_00[1-8]_/i.test(templateId)
        || /^AthenaDance:eid_dancemoves$/i.test(templateId)
        || /^AthenaMusicPack:MusicPack_119_CH1_DefaultMusic$/i.test(templateId)
        || /^AthenaLoadingScreen:LSID_DefaultBG$/i.test(templateId)
        || /^AthenaBackpack:BID_STWHeroNoDefaultBackpack$/i.test(templateId);
}

function resetLocker(profile) {
    const locker = profile.items && profile.items["lawin-loadout"];
    if (!locker || !locker.attributes || !locker.attributes.locker_slots_data) return;
    const slots = locker.attributes.locker_slots_data.slots || {};
    if (slots.Character) slots.Character.items = [""];
    if (slots.Backpack) slots.Backpack.items = [""];
    if (slots.SkyDiveContrail) slots.SkyDiveContrail.items = [""];
    if (slots.LoadingScreen) slots.LoadingScreen.items = [""];
    if (slots.Pickaxe) slots.Pickaxe.items = ["AthenaPickaxe:DefaultPickaxe"];
    if (slots.Glider) slots.Glider.items = ["AthenaGlider:DefaultGlider"];
    if (slots.MusicPack) slots.MusicPack.items = ["AthenaMusicPack:MusicPack_119_CH1_DefaultMusic"];
    if (slots.Dance) slots.Dance.items = ["AthenaDance:eid_dancemoves", "", "", "", "", ""];
    if (slots.ItemWrap) slots.ItemWrap.items = ["", "", "", "", "", "", ""];
    const a = profile.stats.attributes;
    a.favorite_character = "";
    a.favorite_backpack = "";
    a.favorite_pickaxe = "AthenaPickaxe:DefaultPickaxe";
    a.favorite_glider = "AthenaGlider:DefaultGlider";
    a.favorite_skydivecontrail = "";
    a.favorite_musicpack = "AthenaMusicPack:MusicPack_119_CH1_DefaultMusic";
    a.favorite_loadingscreen = "";
    a.favorite_dance = ["AthenaDance:eid_dancemoves", "", "", "", "", ""];
    a.favorite_itemwraps = ["", "", "", "", "", "", ""];
}

function initProgression(profile, season) {
    for (const key of Object.keys(profile.items || {})) {
        const templateId = String(profile.items[key].templateId || "");
        if (isChallengeItem(templateId)) { delete profile.items[key]; continue; }
        if (isCosmetic(templateId) && !isStarterCosmetic(templateId)) delete profile.items[key];
    }
    const a = profile.stats.attributes;
    resetLocker(profile);
    a.titanium_mode = "progression";
    a.titanium_season = season;
    a.season_num = season;
    a.book_purchased = true;
    a.book_level = 0;
    a.book_xp = 0;
    a.battlestars = 0;
    a.battlestars_season_total = 0;
    a.style_points = 0;
    a.level = 0;
    a.accountLevel = 0;
    a.season_match_boost = 0;
    a.season_friend_match_boost = 0;
    a.purchased_bp_offers = [];
    resetCurrency();
}

function initUnlockedAll(profile, season) {
    const a = profile.stats.attributes;
    a.titanium_mode = "unlocked-all";
    a.titanium_season = season;
    a.season_num = season;
    a.book_purchased = true;
    a.book_level = 100;
    a.book_xp = 0;
    a.battlestars = 0;
    a.battlestars_season_total = 0;
    a.level = 100;
    a.accountLevel = 100;
    a.season_match_boost = 0;
    a.season_friend_match_boost = 0;
    resetCurrency();
}

function resetSeason(profile, season) {
    for (const key of Object.keys(profile.items || {})) {
        const templateId = String(profile.items[key].templateId || "");
        if (/^S\d+-/.test(key) || isChallengeItem(templateId)) delete profile.items[key];
    }
    const a = profile.stats.attributes;
    a.titanium_season = season;
    a.season_num = season;
    a.book_purchased = true;
    if (a.titanium_mode === "progression") {
        a.book_level = 0;
        a.book_xp = 0;
        a.battlestars = 0;
        a.battlestars_season_total = 0;
        a.style_points = 0;
        a.level = 0;
        a.accountLevel = 0;
    } else {
        a.book_level = 100;
        a.level = 100;
        a.accountLevel = 100;
    }
}

function apply(profile, memory) {
    if (!profile || profile.profileId !== "athena" || !profile.stats || !profile.stats.attributes) return false;
    const season = seasonNumber(memory);
    const owner = readOwner();
    const mode = profileMode(profile);
    let changed = false;
    const a = profile.stats.attributes;

    if (!a.titanium_mode) {
        if (owner.mode === "progression") initProgression(profile, season);
        else initUnlockedAll(profile, season);
        changed = true;
    } else if (a.titanium_mode !== mode) {
        a.titanium_mode = mode;
        changed = true;
    }

    if (a.titanium_season !== season) {
        resetSeason(profile, season);
        changed = true;
    }

    if (a.book_purchased !== true) {
        a.book_purchased = true;
        changed = true;
    }
    if (a.season_num !== season) {
        a.season_num = season;
        changed = true;
    }

    const bp = loadBattlePass(season);
    if (bp && bp.battlePassOfferId) {
        if (!Array.isArray(a.purchased_bp_offers)) a.purchased_bp_offers = [];
        if (!a.purchased_bp_offers.includes(bp.battlePassOfferId)) {
            a.purchased_bp_offers.push(bp.battlePassOfferId);
            changed = true;
        }
    }

    if (a.titanium_mode === "unlocked-all") {
        if (a.book_level !== 100) { a.book_level = 100; changed = true; }
        if (a.level !== 100) { a.level = 100; changed = true; }
    }
    return changed;
}

function addItem(profile, templateId, quantity, functions) {
    const lower = String(templateId).toLowerCase();
    for (const key of Object.keys(profile.items || {})) {
        if (String(profile.items[key].templateId || "").toLowerCase() === lower) {
            profile.items[key].quantity = Number(profile.items[key].quantity || 0) + Number(quantity || 1);
            return key;
        }
    }
    const key = functions.MakeID();
    profile.items[key] = {
        templateId,
        attributes: {
            max_level_bonus: 0,
            level: 1,
            item_seen: false,
            xp: 0,
            variants: [],
            favorite: false
        },
        quantity: Number(quantity || 1)
    };
    return key;
}

function grantReward(templateId, quantity, profile, core, functions, loot) {
    const lower = String(templateId).toLowerCase();
    if (lower.startsWith("accountresource:") || lower.startsWith("challengebundleschedule:")) return;
    if (lower.startsWith("currency:mtx")) {
        const amount = Number(quantity || 1);
        const key = addCurrency(amount);
        loot.push({ itemType: templateId, itemGuid: key, itemProfile: "common_core", quantity: amount });
        return;
    }
    const target = lower.startsWith("homebasebannericon:") || lower.startsWith("bannertoken:") ? core : profile;
    const key = addItem(target, templateId, quantity, functions);
    loot.push({ itemType: templateId, itemGuid: key, itemProfile: target === core ? "common_core" : "athena", quantity: Number(quantity || 1) });
}

function awardQuest(profile, key, functions, quests, season) {
    const quest = profile.items[key];
    if (!quest || !quest.attributes || quest.attributes.titanium_rewarded === true) return { changed: false, loot: [] };
    quest.attributes.quest_state = "Claimed";
    quest.attributes.titanium_rewarded = true;
    quest.attributes.last_state_change_time = new Date().toISOString();
    let changed = true;
    const loot = [];
    if (profile.stats.attributes.titanium_mode === "progression") {
        const a = profile.stats.attributes;
        const current = Math.max(0, Number(a.book_level || 0));
        const next = Math.min(100, current + 1);
        if (next > current) {
            a.book_level = next;
            a.level = next;
            a.book_xp = 0;
            a.battlestars = 0;
            a.battlestars_season_total = next * 10;
            a.book_purchased = true;
            const bp = loadBattlePass(season);
            if (bp) {
                const core = global.tpf("common_core");
                for (let tier = current + 1; tier <= next; tier++) {
                    const paid = (bp.paidRewards || [])[tier - 1] || {};
                    const free = (bp.freeRewards || [])[tier - 1] || {};
                    for (const rewardSet of [paid, free]) {
                        for (const templateId of Object.keys(rewardSet)) {
                            grantReward(templateId, rewardSet[templateId], profile, core, functions, loot);
                        }
                    }
                }
                save("common_core", core);
            }
        }
    }
    return { changed, loot };
}

function ensureSeasonQuests(profile, season, quests, functions) {
    const data = quests && quests.BattleRoyale && quests.BattleRoyale["Season" + season];
    if (!data) return false;
    let changed = purgeForeignChallenges(profile, season, true);

    for (const key of Object.keys(profile.items || {})) {
        if (/^S\d+-/.test(key) && !key.startsWith("S" + season + "-")) {
            delete profile.items[key];
            changed = true;
        }
    }

    for (const schedule of data.ChallengeBundleSchedules || []) {
        if (profile.items[schedule.itemGuid]) continue;
        profile.items[schedule.itemGuid] = {
            templateId: schedule.templateId,
            attributes: {
                unlock_epoch: new Date().toISOString(),
                max_level_bonus: 0,
                level: 1,
                item_seen: true,
                xp: 0,
                favorite: false,
                granted_bundles: schedule.granted_bundles || []
            },
            quantity: 1
        };
        changed = true;
    }

    for (const bundle of data.ChallengeBundles || []) {
        if (profile.items[bundle.itemGuid]) continue;
        profile.items[bundle.itemGuid] = {
            templateId: bundle.templateId,
            attributes: {
                has_unlock_by_completion: false,
                num_quests_completed: 0,
                level: 0,
                grantedquestinstanceids: bundle.grantedquestinstanceids || [],
                item_seen: true,
                max_allowed_bundle_level: 0,
                num_granted_bundle_quests: (bundle.grantedquestinstanceids || []).length,
                max_level_bonus: 0,
                challenge_bundle_schedule_id: bundle.challenge_bundle_schedule_id || "",
                num_progress_quests_completed: 0,
                xp: 0,
                favorite: false
            },
            quantity: 1
        };
        changed = true;
    }

    for (const quest of data.Quests || []) {
        if (profile.items[quest.itemGuid]) continue;
        const attributes = {
            creation_time: new Date().toISOString(),
            level: -1,
            item_seen: true,
            playlists: [],
            sent_new_notification: true,
            challenge_bundle_id: quest.challenge_bundle_id || "",
            xp_reward_scalar: 1,
            challenge_linked_quest_given: "",
            quest_pool: "",
            quest_state: "Active",
            bucket: "",
            last_state_change_time: new Date().toISOString(),
            challenge_linked_quest_parent: "",
            max_level_bonus: 0,
            xp: 0,
            quest_rarity: "uncommon",
            favorite: false
        };
        for (const objective of quest.objectives || []) {
            const name = typeof objective === "string" ? objective : objective.name;
            attributes["completion_" + String(name).toLowerCase()] = 0;
        }
        profile.items[quest.itemGuid] = { templateId: quest.templateId, attributes, quantity: 1 };
        changed = true;
    }

    let dailyCount = 0;
    for (const item of Object.values(profile.items || {})) {
        if (String(item.templateId || "").toLowerCase().startsWith("quest:athenadaily")) dailyCount++;
    }
    const daily = quests.BattleRoyale.Daily || [];
    if (dailyCount < 3 && daily.length > 0) {
        profile.stats.attributes.quest_manager = profile.stats.attributes.quest_manager || {};
        const used = new Set(Object.values(profile.items || {}).map(item => String(item.templateId || "").toLowerCase()));
        for (let attempt = 0; attempt < 40 && dailyCount < 3; attempt++) {
            const choice = daily[Math.floor(Math.random() * daily.length)];
            if (!choice || used.has(String(choice.templateId).toLowerCase())) continue;
            used.add(String(choice.templateId).toLowerCase());
            const key = functions.MakeID();
            const attributes = {
                creation_time: new Date().toISOString(),
                level: -1,
                item_seen: false,
                playlists: [],
                sent_new_notification: false,
                challenge_bundle_id: "",
                xp_reward_scalar: 1,
                challenge_linked_quest_given: "",
                quest_pool: "",
                quest_state: "Active",
                bucket: "",
                last_state_change_time: new Date().toISOString(),
                challenge_linked_quest_parent: "",
                max_level_bonus: 0,
                xp: 0,
                quest_rarity: "uncommon",
                favorite: false
            };
            for (const objective of choice.objectives || []) {
                const name = typeof objective === "string" ? objective : objective.name;
                attributes["completion_" + String(name).toLowerCase()] = 0;
            }
            profile.items[key] = { templateId: choice.templateId, attributes, quantity: 1 };
            dailyCount++;
            changed = true;
        }
        if (changed) profile.stats.attributes.quest_manager.dailyLoginInterval = new Date().toISOString();
    }
    return changed;
}

function targetFor(templateId, statName, quests, season) {
    const data = quests && quests.BattleRoyale && quests.BattleRoyale["Season" + season];
    const pools = [];
    if (data) pools.push(data.Quests || []);
    pools.push((quests && quests.BattleRoyale && quests.BattleRoyale.Daily) || []);
    for (const pool of pools) {
        for (const quest of pool) {
            if (String(quest.templateId || "").toLowerCase() !== String(templateId || "").toLowerCase()) continue;
            for (const objective of quest.objectives || []) {
                const name = typeof objective === "string" ? objective : objective.name;
                const target = Number(objective && objective.count);
                if (objectivesMatch(name, statName) && Number.isFinite(target) && target > 0) return target;
            }
        }
    }
    if (isDailyQuest(templateId)) {
        for (const [name, count] of Object.entries(DAILY_OBJECTIVE_TARGETS)) {
            if (objectivesMatch(name, statName)) return count;
        }
    }
    return 0;
}

function findObjectiveKey(attributes, statName) {
    for (const name of Object.keys(attributes || {})) {
        if (!/^completion_/i.test(name)) continue;
        if (objectivesMatch(name, statName)) return name;
    }
    return "";
}

function findDefinedObjectiveKey(templateId, statName, quests, season) {
    const data = quests && quests.BattleRoyale && quests.BattleRoyale["Season" + season];
    const pools = [];
    if (data) pools.push(data.Quests || []);
    pools.push((quests && quests.BattleRoyale && quests.BattleRoyale.Daily) || []);
    for (const pool of pools) {
        for (const quest of pool) {
            if (String(quest.templateId || "").toLowerCase() !== String(templateId || "").toLowerCase()) continue;
            for (const objective of quest.objectives || []) {
                const name = typeof objective === "string" ? objective : objective.name;
                if (objectivesMatch(name, statName)) return "completion_" + String(name).toLowerCase();
            }
        }
    }
    return "";
}

function objectiveKeyFor(quest, statName, quests, season) {
    const existing = findObjectiveKey(quest.attributes, statName);
    if (existing) return existing;
    const defined = findDefinedObjectiveKey(quest.templateId, statName, quests, season);
    if (!defined) return "";
    quest.attributes[defined] = 0;
    return defined;
}

function isComplete(profile, key, quests, season) {
    const attributes = profile.items[key].attributes || {};
    let hasObjective = false;
    for (const name of Object.keys(attributes)) {
        if (!name.toLowerCase().startsWith("completion_")) continue;
        hasObjective = true;
        const target = targetFor(profile.items[key].templateId, name.slice(11), quests, season);
        const value = Number(attributes[name] || 0);
        // An unknown target must never count as complete. Completing a daily at
        // 1/4 was what made the client discard or reset its visible progress.
        if (target <= 0 || value < target) return false;
    }
    return hasObjective;
}

function applyAdvances(profile, advance, functions, quests, season) {
    let changed = false;
    const loot = [];
    if (!Array.isArray(advance)) return { changed, loot };
    for (const update of advance) {
        if (!update || typeof update !== "object") continue;
        const statName = String(update.statName || update.name || "").trim();
        const parsedCount = Number(update.count);
        if (!statName || !Number.isFinite(parsedCount)) continue;
        const count = Math.max(0, parsedCount);
        for (const key of Object.keys(profile.items || {})) {
            const quest = profile.items[key];
            if (!quest || !quest.attributes || quest.attributes.quest_state === "Claimed") continue;
            const objectiveKey = objectiveKeyFor(quest, statName, quests, season);
            if (!objectiveKey) continue;
            if (count > Number(quest.attributes[objectiveKey] || 0)) {
                quest.attributes[objectiveKey] = count;
                changed = true;
            }
            if (isComplete(profile, key, quests, season)) {
                const result = awardQuest(profile, key, functions, quests, season);
                changed = changed || result.changed;
                loot.push.apply(loot, result.loot);
            }
        }
    }
    return { changed, loot };
}

function respond(res, profile, baseRevision, extra) {
    res.json(Object.assign({
        profileRevision: profile.rvn || 0,
        profileId: "athena",
        profileChangesBaseRevision: baseRevision,
        profileChanges: [{ changeType: "fullProfileUpdate", profile }],
        profileCommandRevision: profile.commandRevision || 0,
        serverTime: new Date().toISOString(),
        responseVersion: 1
    }, extra || {}));
}

function clientQuestLogin(req, res, functions, quests) {
    const profile = global.tpf("athena");
    const base = profile.rvn || 0;
    const memory = functions.GetVersionInfo(req);
    const season = seasonNumber(memory);
    let changed = apply(profile, memory);
    changed = ensureSeasonQuests(profile, season, quests, functions) || changed;
    const result = applyAdvances(profile, req.body && req.body.advance, functions, quests, season);
    changed = result.changed || changed;
    if (changed) {
        bump(profile);
        save("athena", profile);
    }
    respond(res, profile, base, result.loot.length ? { notifications: [{ type: "questClaim", primary: true, loot: { items: result.loot } }] } : undefined);
}

function updateQuestObjectives(req, res, functions, quests) {
    const profile = global.tpf("athena");
    const base = profile.rvn || 0;
    const memory = functions.GetVersionInfo(req);
    const season = seasonNumber(memory);
    let changed = apply(profile, memory);
    changed = ensureSeasonQuests(profile, season, quests, functions) || changed;
    const result = applyAdvances(profile, req.body && req.body.advance, functions, quests, season);
    changed = result.changed || changed;
    if (changed) {
        bump(profile);
        save("athena", profile);
    }
    respond(res, profile, base, result.loot.length ? { notifications: [{ type: "questClaim", primary: true, loot: { items: result.loot } }] } : undefined);
}

function findQuest(profile, id) {
    if (!id) return null;
    if (profile.items[id]) return id;
    const lower = String(id).toLowerCase();
    for (const key of Object.keys(profile.items)) {
        if (String(profile.items[key].templateId || "").toLowerCase() === lower) return key;
    }
    return null;
}

function claimAthenaQuest(req, res, functions, quests) {
    const profile = global.tpf("athena");
    const base = profile.rvn || 0;
    const memory = functions.GetVersionInfo(req);
    const season = seasonNumber(memory);
    let changed = apply(profile, memory);
    const key = findQuest(profile, req.body && req.body.questId);
    let loot = [];
    if (key && isComplete(profile, key, quests, season)) {
        const result = awardQuest(profile, key, functions, quests, season);
        changed = changed || result.changed;
        loot = result.loot;
    }
    if (changed) {
        bump(profile);
        save("athena", profile);
    }
    respond(res, profile, base, loot.length ? { notifications: [{ type: "questClaim", primary: true, questId: profile.items[key] ? profile.items[key].templateId : "", loot: { items: loot } }] } : undefined);
}

module.exports = {
    Apply: apply,
    ClientQuestLogin: clientQuestLogin,
    UpdateQuestObjectives: updateQuestObjectives,
    ClaimAthenaQuest: claimAthenaQuest
};
