import express from "express";
const app = express.Router();
import {nextShopRefresh} from '../Utils/syndicateShopRotation.js';
import Utils from "../Utils/Utils.js";
import fs from "node:fs";
import {loadChallengeData} from "../Utils/syndicateSeasonChallenges.js";
// Syndicate: extra calendar event flags per Fortnite build (e.g. a live event such as Season X's
// Batman crossover). Edit src/local/tiltedEventFlags.json; it is re-read when it changes.
const eventFlagFile = new URL("../local/tiltedEventFlags.json", import.meta.url);
let eventFlagCache = { mtime: 0, data: {} };
function extraEventFlags(build) {
  try {
    const mtime = fs.statSync(eventFlagFile).mtimeMs;
    if (mtime !== eventFlagCache.mtime) eventFlagCache = { mtime, data: JSON.parse(fs.readFileSync(eventFlagFile, "utf8")) };
  } catch { return []; }
  const key = Number(build).toFixed(2), list = eventFlagCache.data[key] || eventFlagCache.data[String(build)] || [];
  return Array.isArray(list) ? list.filter(f => typeof f === "string" && /^[A-Za-z0-9_.]{3,120}$/.test(f)) : [];
}
app.get("/fortnite/api/calendar/v1/timeline", (req, res) => {
  const memory = Utils.GetVersion(req);
  let activeEvents = [
    {
      eventType: `EventFlag.Season${memory.season}`,
      activeUntil: "9999-01-01T00:00:00.000Z",
      activeSince: "2019-01-01T00:00:00.000Z",
    },
    {
      eventType: `EventFlag.${memory.lobby}`,
      activeUntil: "9999-01-01T00:00:00.000Z",
      activeSince: "2020-01-01T00:00:00.000Z",
    },
  ];
  // Challenge schedules that only show while their calendar event is on (Chapter 2 missions), from the
  // game server's exported challenge data for this season.
  try {
    for (const schedule of loadChallengeData(Number(memory.season))?.schedules || [])
      if (typeof schedule.calendar === "string" && /^[A-Za-z0-9_.]{3,120}$/.test(schedule.calendar) && !activeEvents.some(e => e.eventType === schedule.calendar))
        activeEvents.push({ eventType: schedule.calendar, activeUntil: "9999-01-01T00:00:00.000Z", activeSince: "2019-01-01T00:00:00.000Z" });
  } catch { }
  for (const flag of extraEventFlags(memory.build))
    activeEvents.push({ eventType: flag.startsWith("EventFlag.") ? flag : `EventFlag.${flag}`, activeUntil: "9999-01-01T00:00:00.000Z", activeSince: "2019-01-01T00:00:00.000Z" });
  const isoDate = nextShopRefresh();
  res.json({
    channels: {
      "client-matchmaking": {
        states: [],
        cacheExpire: "9999-01-01T00:00:00.000Z",
      },
      "client-events": {
        states: [
          {
            validFrom: "0001-01-01T00:00:00.000Z",
            activeEvents: activeEvents,
            state: {
              activeStorefronts: [],
              eventNamedWeights: {},
              seasonNumber: memory.season,
              seasonTemplateId: `AthenaSeason:athenaseason${memory.season}`,
              matchXpBonusPoints: 0,
              seasonBegin: "2020-01-01T00:00:00Z",
              seasonEnd: "9999-01-01T00:00:00Z",
              seasonDisplayedEnd: "9999-01-01T00:00:00Z",
              weeklyStoreEnd: isoDate,
              stwEventStoreEnd: "9999-01-01T00:00:00.000Z",
              stwWeeklyStoreEnd: "9999-01-01T00:00:00.000Z",
              sectionStoreEnds: {
                Featured: isoDate,
                Daily: isoDate,
              },
              dailyStoreEnd: isoDate,
            },
          },
        ],
        cacheExpire: isoDate,
      },
    },
    eventsTimeOffsetHrs: 0,
    cacheIntervalMins: 10,
    currentTime: new Date().toISOString(),
  });
});
export default app;
