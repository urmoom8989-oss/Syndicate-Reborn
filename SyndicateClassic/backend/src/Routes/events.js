import express from "express";
import {clientBuild} from "../Utils/syndicateBuild.js";
import {enabledModes} from "../Utils/syndicateSeasonModes.js";
import {customTournaments} from "../Utils/syndicateNewsSettings.js";
import fs from "fs";
import axios from "axios";
import { MongoClient } from "mongodb";
import mongoose from 'mongoose';
import Utils from "../Utils/Utils.js";
import log from "../Utils/log.js";
import path from "path";
import { dirname } from 'dirname-filename-esm';
import { verifyToken } from "../User/tokenManager/tokenVerify.js";
import { v4 } from 'uuid';

import dotenv from "dotenv";
dotenv.config();

const __dirname = dirname(import.meta);
const app = express.Router();

const DatabaseURL = process.env.MONGO_URI;
const NameFromURL = DatabaseURL.split("/");
const DatabaseName = NameFromURL[3];
const DatabaseCollectionName = 'profiles';

const userSchema = new mongoose.Schema({
    username: String,
    accountId: String,
    tournamentDetails: 
    {
        kills: { type: Number, default: 0 },
        placement: { type: Number, default: 0 },
        points: { type: Number, default: 0 },
        wins: { type: Number, default: 0 },
        matchesPlayed: { type: Number, default: 0 },
        matches: { type: Array, default: [] },
        played: { type: Boolean, default: false }
    }
});

const profileSchema = new mongoose.Schema({
    accountId: String,
    profiles: {
        athena: {
            stats: {
                attributes: {
                    arena_hype: { type: Number, default: 0 }
                }
            }
        }
    }
});

const User = mongoose.model('users', userSchema);
const Profile = mongoose.model('profiles', profileSchema);

async function GetPlayerHype(accountid) {
    const client = new MongoClient(DatabaseURL, { useNewUrlParser: true, useUnifiedTopology: true });

    try {
        await client.connect();
        const db = client.db(DatabaseName);
        const collection = db.collection(DatabaseCollectionName);

        const profile = await collection.findOne({ 'accountId': accountid });
        if (profile && profile.profiles && profile.profiles.athena && profile.profiles.athena.stats && profile.profiles.athena.stats.attributes) {
            return profile.profiles.athena.stats.attributes.arena_hype || 0;
        } else {
            throw new Error('Profile not found or malformed!');
        }
    } catch (error) {
        console.error(`Error fetching player hype: ${error}`);
        return 0;
    } finally {
        await client.close();
    }
}

// Arena (and its Compete-tab events) exists from Season 8 through Chapter 2 Season 2 (12). Events.json is fully
// templated by season (syndicatevalueseason -> S12), and 12.50 plays it from the single Arena playlist tile.
function SyndicateArenaSupported(req) {
    let season = 0;
    try { season = Utils.GetVersion(req).season; } catch { season = 0; }
    return season >= 8 && season <= 12;
}

app.get("/api/v1/events/Fortnite/download/:accountid", verifyToken, async (req, res) => {
    if (!SyndicateArenaSupported(req))
        return res.json({ player: { gameId: "Fortnite", accountId: req.params.accountid, tokens: [], teams: {}, pendingPayouts: [], pendingPenalties: {}, persistentScores: {}, groupIdentity: {} }, events: [], templates: [], scores: [] });
    const seasonNum = Utils.GetVersion(req).season;
    const accountid = req.params.accountid;
    const user = await User.findOne({ accountId: accountid });

    if (!user) {
        res.send("No user found!");
    }

    const username = user.username;
    const arena = path.join(__dirname, `../local/events/Events.json`);
    let currentSeason = "S" + seasonNum;

    let playerHype;
    try {
        playerHype = await GetPlayerHype(accountid);
    }
    catch (err) {
        log.error("Error getting player hype! Error :", err);
        res.status(500).send('Internal server error');
        return;
    }

    let playerDivision = `"LG_ARENA_${currentSeason}_Division1"`;

    if (playerHype >= 16000) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2", "LG_ARENA_${currentSeason}_Division3", "LG_ARENA_${currentSeason}_Division4", "LG_ARENA_${currentSeason}_Division5", "LG_ARENA_${currentSeason}_Division6", "LG_ARENA_${currentSeason}_Division7", "LG_ARENA_${currentSeason}_Division8", "LG_ARENA_${currentSeason}_Division9", "LG_ARENA_${currentSeason}_Division10"`;
    } else if (playerHype >= 12000) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2", "LG_ARENA_${currentSeason}_Division3", "LG_ARENA_${currentSeason}_Division4", "LG_ARENA_${currentSeason}_Division5", "LG_ARENA_${currentSeason}_Division6", "LG_ARENA_${currentSeason}_Division7", "LG_ARENA_${currentSeason}_Division8", "LG_ARENA_${currentSeason}_Division9"`;
    } else if (playerHype >= 6000) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2", "LG_ARENA_${currentSeason}_Division3", "LG_ARENA_${currentSeason}_Division4", "LG_ARENA_${currentSeason}_Division5", "LG_ARENA_${currentSeason}_Division6", "LG_ARENA_${currentSeason}_Division7", "LG_ARENA_${currentSeason}_Division8"`;
    } else if (playerHype >= 4000) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2", "LG_ARENA_${currentSeason}_Division3", "LG_ARENA_${currentSeason}_Division4", "LG_ARENA_${currentSeason}_Division5", "LG_ARENA_${currentSeason}_Division6", "LG_ARENA_${currentSeason}_Division7"`;
    } else if (playerHype >= 2500) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2", "LG_ARENA_${currentSeason}_Division3", "LG_ARENA_${currentSeason}_Division4", "LG_ARENA_${currentSeason}_Division5", "LG_ARENA_${currentSeason}_Division6"`;
    } else if (playerHype >= 1500) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2", "LG_ARENA_${currentSeason}_Division3", "LG_ARENA_${currentSeason}_Division4", "LG_ARENA_${currentSeason}_Division5"`;
    } else if (playerHype >= 1000) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2", "LG_ARENA_${currentSeason}_Division3", "LG_ARENA_${currentSeason}_Division4"`;
    } else if (playerHype >= 500) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2", "LG_ARENA_${currentSeason}_Division3"`;
    } else if (playerHype >= 250) {
        playerDivision = `"LG_ARENA_${currentSeason}_Division1", "LG_ARENA_${currentSeason}_Division2"`;
    }

    fs.readFile(arena, 'utf-8', (err, data) => {
        if (err) {
            log.error("Error reading file:", err);
            res.status(500).send('Error reading file!!!');
            return;
        }

        let modifiedData = data.replace(/syndicatevaluehype/g, playerHype)
            .replace(/syndicatevalueseason/g, currentSeason)
            .replace(/syndicatevalueaccountid/g, accountid)
            .replace(/syndicatevaluedivision/g, playerDivision);
        // Seasons 8 to X: the public Arena event is the lobby's Arena tile and carries the division and Hype, so the
        // plain Arena playlist tile is left out of the lobby there (syndicateLegacy111Lobby.js). Chapter 2 (11.00, 12.50)
        // plays Arena from the plain tile, and Arena turned off in the launcher hides the event tile too: the event
        // stays (the in-match counter / TournamentId needs it) but unlisted.
        { const ua = String(req.headers['user-agent'] || ''), build = clientBuild(ua);
          if (build === '11.00' || build === '12.50' || !enabledModes(build).includes('arenasolo'))
              modifiedData = modifiedData.replace(/"visibility": "public"/g, '"visibility": "unlisted"'); }

        let events;
        try {
            events = JSON.parse(modifiedData);
            // Arena Duos turned on in the launcher: its own event (the Solo one on the Duos playlist), so the Duos
            // tile shows the division and Hype too. Both share the one Hype score the backend keeps.
            { const build = clientBuild(String(req.headers['user-agent'] || ''));
              if (enabledModes(build).includes('arenaduos')) SyndicateAddArenaDuos(events); }
            // Tournaments made in the launcher (Settings > News & tournaments).
            SyndicateAddCustomTournaments(events, clientBuild(String(req.headers['user-agent'] || '')));
        } catch (parseErr) {
            log.error("Error parsing JSON:", parseErr);
            res.status(500).send('Error parsing JSON!!!');
            return;
        }

        log.arena(username + " sent an arena JSON request!");
        res.json(events);
    });
    if (process.env.EVENT_TYPE === "Tournament") {
        log.arena("Tournaments not set-up yet!");
    }
    else {
        log.arena("Arena is disabled, not sending json data!");
    }
});

// A launcher tournament: its own public event in the Compete tab, open to everyone between its begin and end time,
// playing the chosen mode's playlist and scored like Arena (placement and eliminations). It never touches Hype.
function SyndicateAddCustomTournaments(data, build) {
    const solo = (data.events || []).find(e => e.displayDataId === 'LG_ARENA_SOLO');
    const soloTemplate = (data.templates || []).find(t => t.playlistId === 'Playlist_ShowdownAlt_Solo');
    for (const t of customTournaments(build)) {
        if (data.events.some(e => e.displayDataId === t.displayId)) continue;
        const windowId = 'Syndicate_' + t.id + '_Window1';
        const begin = t.begin.toISOString(), end = t.end.toISOString();
        const window = {
            ...(solo?.eventWindows?.[0] || {}), additionalRequirements: [], beginTime: begin, endTime: end, countdownBeginTime: begin,
            blackoutPeriods: [], canLiveSpectate: false, eventTemplateId: windowId, eventWindowId: windowId, isTBD: false,
            metadata: {RoundType: 'Qualifiers'}, payoutDelay: 1, requireAllTokens: [], requireAllTokensCaller: [], requireAnyTokens: [],
            requireAnyTokensCaller: [], requireNoneTokensCaller: [], round: 0,
            scoreLocations: [{scoreMode: 'window', scoreId: windowId, leaderboardId: windowId, useIndividualScores: false}],
            teammateEligibility: 'any', visibility: 'public',
        };
        data.events.push({
            ...(solo || {}), gameId: 'Fortnite', eventId: 'syndicate_cup_' + t.id.toLowerCase(), displayDataId: t.displayId,
            announcementTime: begin, beginTime: begin, endTime: end, eventGroup: '', link: null,
            metadata: {TrackedStats: ['PLACEMENT_STAT_INDEX', 'TEAM_ELIMS_STAT_INDEX', 'MATCH_PLAYED_STAT'], minimumAccountLevel: 0},
            eventWindows: [window],
        });
        data.templates.push({
            eventTemplateId: windowId, gameId: 'Fortnite', matchCap: -1, playlistId: t.playlist,
            payoutTable: {ranks: [], scoreId: windowId, scoringType: 'value'},
            scoringRules: soloTemplate?.scoringRules || [],
        });
    }
}

function SyndicateAddArenaDuos(data) {
    const solo = (data.events || []).find(e => e.displayDataId === 'LG_ARENA_SOLO');
    if (!solo || data.events.some(e => e.displayDataId === 'LG_ARENA_DUOS')) return;
    const duosId = id => String(id).replace(/LGSolo$/, 'LGDuos');
    const duos = JSON.parse(JSON.stringify(solo));
    duos.eventId = solo.eventId + '_duos';
    duos.displayDataId = 'LG_ARENA_DUOS';
    for (const w of duos.eventWindows || []) { w.eventTemplateId = duosId(w.eventTemplateId); w.eventWindowId = duosId(w.eventWindowId); }
    data.events.push(duos);
    for (const t of (data.templates || []).filter(t => t.playlistId === 'Playlist_ShowdownAlt_Solo')) {
        const copy = JSON.parse(JSON.stringify(t));
        copy.eventTemplateId = duosId(t.eventTemplateId);
        copy.playlistId = 'Playlist_ShowdownAlt_Duos';
        data.templates.push(copy);
    }
}

// Syndicate: Arena division tokens. The client re-reads these after every Arena
// match (RefreshPlayerData); an empty list hides the Arena playlist.
const SyndicateArenaThresholds = [250, 500, 1000, 1500, 2500, 4000, 6000, 12000, 16000];

function SyndicateArenaSeason(req) {
    let season = 0;
    try { season = Utils.GetVersion(req).season; } catch { season = 0; }
    if (!season) season = parseInt(process.env.MAIN_SEASON, 10) || 0;
    return "S" + season;
}

function SyndicateArenaTokens(season, hype) {
    let divisions = 1;
    for (const t of SyndicateArenaThresholds) if ((hype || 0) >= t) divisions++;
    const tokens = [];
    for (let i = 1; i <= divisions; i++) tokens.push(`LG_ARENA_${season}_Division${i}`);
    return tokens;
}

async function SyndicateArenaPlayer(req, accountId) {
    const season = SyndicateArenaSeason(req);
    let hype = 0;
    try { hype = await GetPlayerHype(accountId); } catch { hype = 0; }
    return {
        accountId,
        gameId: "Fortnite",
        groupIdentity: {},
        pendingPayouts: [],
        pendingPenalties: {},
        persistentScores: { Hype: hype },
        teams: { "floating:Hype": [accountId] },
        tokens: SyndicateArenaSupported(req) ? SyndicateArenaTokens(season, hype) : []
    };
}

app.get("/api/v1/players/Fortnite/tokens", async (req, res) => {
    try {
        let ids = req.query.teamAccountIds || req.query.accountIds || [];
        if (!Array.isArray(ids)) ids = String(ids).split(",");
        ids = ids.map(x => String(x).trim()).filter(Boolean);
        const accounts = [];
        for (const id of ids) {
            const player = await SyndicateArenaPlayer(req, id);
            accounts.push({ accountId: id, tokens: player.tokens });
        }
        res.json({ accounts });
    } catch (err) {
        log.error("Arena tokens error: " + err);
        res.json({ accounts: [] });
    }
});

app.get("/api/v1/leaderboards/Fortnite/:eventId/:eventWindowId/:accountId", async (req, res) => {
    try {
        const { eventId, eventWindowId, accountId } = req.params;
        const filePath = path.join(__dirname, "../local/events/leaderboard.json");

        if (!fs.existsSync(filePath)) {
            console.error(`leaderboard.json not found at ${filePath}`);
            return res.status(404).json({ error: "Leaderboard data not found" });
        }

        const users = await User.find({});
        if (!users || users.length === 0) {
            console.warn("No users found in database");
            return res.status(404).json({ error: "No users found" });
        }

        const leaderboard = {
            entries: [],
            eventId,
            eventWindowId,
            gameId: "Fortnite",
            page: 0,
            totalPages: 1,
            updatedTime: new Date().toISOString(),
        };

        for (const user of users) {
            if (user.accountId !== accountId) continue;
            if (!user.tournamentDetails?.played) continue;

            const matches = user.tournamentDetails?.matches ?? [];
            if (!Array.isArray(matches) || matches.length === 0) {
                continue;
            }

            const pointBreakdown = {};
            const sessionHistory = [];

            let totalPoints = 0;
            let totalKills = 0;

            for (const match of matches) {
                const isValid =
                    match.placement != null &&
                    match.placementPoints != null &&
                    match.kills != null &&
                    match.killPoints != null &&
                    match.timeAlive != null &&
                    match.victory != null;

                if (!isValid) {
                    console.log(
                        `⚠️ Incomplete match skipped for user ${user.accountId}:`,
                        JSON.stringify(match, null, 2)
                    );
                    continue;
                }

                const placementKey = `PLACEMENT_STAT_INDEX:${match.placement}`;
                if (!pointBreakdown[placementKey]) {
                    pointBreakdown[placementKey] = {
                        pointsEarned: match.placementPoints,
                        timesAchieved: 1,
                    };
                } else {
                    pointBreakdown[placementKey].pointsEarned += match.placementPoints;
                    pointBreakdown[placementKey].timesAchieved += 1;
                }

                totalKills += match.kills;
                totalPoints += match.placementPoints + match.killPoints;

                sessionHistory.push({
                    endTime: new Date().toISOString(),
                    sessionId: v4(),
                    trackedStats: {
                        MATCH_PLAYED_STAT: 1,
                        PLACEMENT_STAT_INDEX: match.placement,
                        TEAM_ELIMS_STAT_INDEX: match.kills,
                        TIME_ALIVE_STAT: match.timeAlive,
                        VICTORY_ROYALE_STAT: match.victory,
                    },
                });
            }

            if (totalKills > 0) {
                pointBreakdown["TEAM_ELIMS_STAT_INDEX:0"] = {
                    pointsEarned: totalKills,
                    timesAchieved: totalKills,
                };
            }

            leaderboard.entries.push({
                eventId,
                eventWindowId,
                gameId: "Fortnite",
                percentile: 0,
                pointBreakdown,
                pointsEarned: totalPoints,
                rank: 1,
                score: Date.now(),
                scoreKey: {
                    _scoreId: null,
                    eventId,
                    eventWindowId,
                    gameId: "Fortnite",
                },
                sessionHistory,
                teamAccountIds: [user.accountId],
                teamId: user.accountId,
                unscoredSessions: {},
            });
        }

        try {
            fs.writeFileSync(filePath, JSON.stringify(leaderboard, null, 2), "utf8");
        } catch (writeError) {
            console.error(`Failed to write leaderboard.json:`, writeError);
            return res.status(500).json({ error: "Failed to save leaderboard data" });
        }

        return res.json(leaderboard);
    } catch (error) {
        console.error(
            `Error in /api/v1/leaderboards/Fortnite/:eventId/:eventWindowId/:accountId:`,
            error
        );
        return res.status(500).json({ error: "Internal server error" });
    }
});

app.get("/api/v1/events/Fortnite/data/", async (req, res) => {
    res.json({});
});

app.get("/api/v1/events/Fortnite/:eventId/:eventWindowId/history/:accountId", async (req, res) => {
    res.json({});
});

app.get("/api/v1/events/Fortnite/:eventId/history/:accountId", async (req, res) => {
    try {
        const history = JSON.parse(
            fs.readFileSync(path.join(__dirname, "../local/events/history.json"), {
                encoding: "utf8",
            })
        );

        history[0].scoreKey.eventId = req.params.eventId;
        history[0].teamId = req.params.accountId;
        history[0].teamAccountIds.push(req.params.accountId);

        const user = await User.findOne({ accountId: req.params.accountId });
        if (!user) {
            console.error(`User not found for accountId: ${req.params.accountId}`);
            return res.status(404).json({ error: "User not found" });
        }

        const tournamentDetails = user.tournamentDetails || {
            kills: 0,
            placement: 0,
            points: 0,
            wins: 0,
            matchesPlayed: 0,
            matches: [],
        };

        const dedicatedPlayer = {
            TeamId: user.username || "Unknown",
            TeamAccountId: user.accountId,
            PointsEarned: tournamentDetails.points,
            SessionHistory: user.username ? `${user.username}sessionId` : "unknownSessionId",
        };

        const eventId = "tilted_cup";
        const windowId = "tilted_cup1";
        const rank = 5;
        const percentile = 0;
        const pointBreakdown = {
            eliminations: tournamentDetails.kills,
            placements: tournamentDetails.placement,
        };

        const response = [
            {
                scoreKey: {
                    gameId: "Fortnite",
                    eventId,
                    eventWindowId: windowId,
                    _scoreId: null,
                },
                teamId: dedicatedPlayer.TeamId,
                teamAccountIds: Array.isArray(dedicatedPlayer.TeamAccountId)
                    ? dedicatedPlayer.TeamAccountId
                    : [dedicatedPlayer.TeamAccountId],
                liveSessionId: null,
                pointsEarned: dedicatedPlayer.PointsEarned,
                eventWindowId: windowId,
                score: dedicatedPlayer.PointsEarned,
                gameId: "Fortnite",
                eventId,
                rank: rank,
                percentile: Math.round(percentile),
                pointBreakdown: pointBreakdown,
                sessionHistory: dedicatedPlayer.SessionHistory,
                unscoredSessions: [],
            },
        ];

        return res.json(response);
    } catch (error) {
        console.error(`Error in /api/v1/events/Fortnite/:eventId/history/:accountId:`, error);
        return res.status(500).json({ error: "Internal server error" });
    }
});

app.get("/api/v1/events/Fortnite/:windowId/history/:accountId", async (req, res) => {
    res.json({});
});

app.get("/api/v1/players/Fortnite/:accountId", async (req, res) => {
    try {
        res.json(await SyndicateArenaPlayer(req, req.params.accountId));
    } catch (err) {
        log.error("Arena player refresh error: " + err);
        const season = SyndicateArenaSeason(req);
        res.json({
            accountId: req.params.accountId,
            gameId: "Fortnite",
            groupIdentity: {},
            pendingPayouts: [],
            pendingPenalties: {},
            persistentScores: { Hype: 0 },
            teams: { "floating:Hype": [req.params.accountId] },
            tokens: SyndicateArenaSupported(req) ? SyndicateArenaTokens(season, 0) : []
        });
    }
});

export default app;