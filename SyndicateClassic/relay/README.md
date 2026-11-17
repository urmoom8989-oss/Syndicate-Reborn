# Syndicate Cloudflare Worker (`springs-relay`)

`worker.js` runs as the Cloudflare Worker `springs-relay`
(`https://your-relay.example.workers.dev`). It does three things:

| Path | What |
|---|---|
| `POST /interactions` | The Syndicate Discord bot: `/code` commands (admins only) |
| `POST /redeem`, `POST /redeem/release` | Syndicate backends redeem codes here (`backend/src/Utils/syndicateRedeem.js`) |
| `GET /setup?key=SETUP_KEY` | One time: registers the bot's slash commands and adds the older Psycho Bandit codes |
| `POST /error` | Launcher error reports (`DISCORD_ERRORS_WEBHOOK`), unchanged |
| `GET /tester/start`, `GET /tester/callback`, `POST /tester/poll`, `POST /tester/check`, `POST /tester/publish` | Tester launcher: Discord sign-in, Tester role check (every start + every 20 min), newest tester build |
| `POST /report` | Bot-learning match feed: forwards consenting players' pseudonymous gameplay reports to the `DISCORD_WEBHOOK` secret (answers 503 if it is not set) |

## Redeem codes

- `/code create reward amount note` makes 1-25 one-time codes for Psycho Bandit, Travis Scott, Axe of Champions,
  Leviathan Axe, Polo Prodigy, Kratos or Minty Axe. The codes are shown once, only to the admin who made them.
- `/code check code`, `/code revoke code`, `/code list [reward]`, `/code stats`.
- Only the Discord user IDs in `ADMINS` (in `worker.js`) plus the optional `ADMIN_IDS` setting can use them.
  The command is also hidden from members without the Administrator permission.
- Codes are stored only as SHA-256 hashes in the D1 database. `POST /redeem` marks a code used in one database
  statement, so the first player to redeem it gets the item and everyone after is refused, on any PC.
- New rewards: add them to `REWARDS` in `worker.js` and in `backend/src/Utils/syndicateRedeemItems.js` (same id),
  deploy, then open `/setup?key=...` again so the bot's choice list updates.

## Setup (once)

1. Cloudflare dashboard > Storage & Databases > D1 > Create database, name `syndicate-codes`.
2. Workers & Pages > `springs-relay` > Settings > Bindings > Add > D1 database: variable name `DB`, database `syndicate-codes`.
3. Settings > Variables and Secrets > Add:
   - `DISCORD_PUBLIC_KEY` (Text): the app's Public Key (Developer Portal > General Information)
   - `DISCORD_APP_ID` (Text): the Application ID
   - `DISCORD_TOKEN` (Secret): the bot token (Developer Portal > Bot > Reset Token). Never put it in code or chat.
   - `SETUP_KEY` (Secret): any long random word
   Keep `DISCORD_ERRORS_WEBHOOK`.
4. Edit code > replace everything with `worker.js` > Deploy.
5. Developer Portal > General Information > Interactions Endpoint URL:
   `https://your-relay.example.workers.dev/interactions` > Save Changes (Discord checks it right away).
6. Open `https://your-relay.example.workers.dev/setup?key=<your SETUP_KEY>` once. It should say
   "Slash commands registered."
7. In Discord type `/code create`.

## Tester launcher (once)

The Tester EXE (`MAKE TESTER RELEASE.cmd`) only opens for members with a Tester role in the Syndicate Discord.

1. Developer Portal > your app > **OAuth2**: copy the **Client Secret**, and under **Redirects** add
   `https://your-relay.example.workers.dev/tester/callback` > Save.
2. The bot must be **in the Syndicate server** (it reads a member's roles). If it only has slash commands, invite
   it with the `bot` scope (no permissions needed).
3. In Discord turn on Developer Mode (Settings > Advanced), then right-click the server > Copy Server ID, and
   Server Settings > Roles > right-click Tester > Copy Role ID.
4. Worker > Settings > Variables and Secrets > Add:
   - `DISCORD_CLIENT_SECRET` (Secret): the Client Secret from step 1
   - `TESTER_GUILD_ID` (Text): the server ID
   - `TESTER_ROLE_IDS` (Text): the Tester role ID (add more, comma separated, e.g. a Dev role)
   - `TESTER_PUBLISH_KEY` (Secret): any long random word
5. Deploy `worker.js` again (the tables are created on the first request).
6. On each dev PC that makes tester builds, save that same random word in
   `%LOCALAPPDATA%\ProjectTilted\dev\tester-publish-key.txt` (never commit it).

How it works: first start opens Discord sign-in in the browser (scope `identify` only); the relay checks the role with
the bot and gives that PC a token (it does not work on another PC; 3 PCs per Discord account). Every start and every
20 minutes the launcher asks again, so giving someone the Tester role is all a new tester needs, and removing it locks
them out: the launcher closes and removes its program files (accounts are kept). Offline, it keeps working for 24 hours
after its last successful check. `MAKE TESTER RELEASE.cmd` uploads a GitHub pre-release and tells the relay, and
every tester's launcher updates to it on its next start.
