// node --test src/Utils/test/syndicateSeasonModes.test.js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { availableModes, defaultModes, enabledModes, modeEnabled, stwEnabledFor, lobbyTiles, lobbyRowCount } from '../syndicateSeasonModes.js';

const env = { TILTED_DEVKIT: '' };

test('Creative is offered from Season 7, Arena from Season 8, 50v50 and Playground from Season 4', () => {
  assert.equal(availableModes('6.20').includes('creative'), false);
  for (const b of ['7.40', '8.50', '9.40', '10.40', '11.00', '12.50']) assert.ok(availableModes(b).includes('creative'), b);
  assert.equal(availableModes('7.40').includes('arenasolo'), false);
  assert.ok(availableModes('8.50').includes('arenasolo'));
  assert.deepEqual(availableModes('4.5'), ['solo', 'duo', 'squad', '50v50', 'gauntlet', 'solidgold', 'blitz', 'snipers', 'close', 'highexplosives', 'playground', 'stw']);
  // A limited-time mode stays in every later season (their playlists are still in the game), never earlier ones.
  for (const b of ['6.20', '7.40', '8.50', '9.40', '10.31', '10.40']) assert.ok(availableModes(b).includes('50v50'), b);
  for (const b of ['5.10', '6.20', '7.40', '8.50', '9.40', '10.40']) assert.ok(availableModes(b).includes('gauntlet'), b);
  for (const b of ['5.10', '7.40', '8.50', '10.40']) assert.ok(availableModes(b).includes('playground'), b);
  assert.deepEqual(availableModes('10.40'), ['solo', 'duo', 'squad', 'arenasolo', '50v50', 'gauntlet', 'solidgold', 'blitz', 'snipers', 'close',
    'highexplosives', 'disco', 'teamrumble', 'foodfight', 'floorislava', 'arenaduos', 'playground', 'creative', 'stw']);
  // Floor Is Lava is Season 8's solo playlist (Playlist_Fill_Solo), which 8.50 has too; 7.40 is before it came out.
  assert.ok(availableModes('8.50').includes('floorislava'));
  assert.equal(availableModes('7.40').includes('floorislava'), false);
  assert.equal(availableModes('3.5.1').includes('gauntlet'), false);
  assert.equal(availableModes('1.8.2').includes('50v50'), false);
  assert.ok(availableModes('9.40').includes('arenaduos'));
  for (const b of ['1.7.2', '1.8.2', '1.11', '3.5.1', '10.40']) assert.ok(availableModes(b).includes('stw'), b);
  assert.deepEqual(availableModes('19.00'), []);
});

test('no saved choice keeps the old lobby lists', () => {
  assert.deepEqual(enabledModes('7.40', { builds: {}, env }), ['solo', 'duo', 'squad']);
  assert.deepEqual(enabledModes('10.40', { builds: {}, env }), ['solo', 'duo', 'squad', 'arenasolo']);
  assert.deepEqual(enabledModes('11.00', { builds: {}, env: { TILTED_DEVKIT: '1' } }), ['solo', 'duo', 'squad', 'arenasolo', 'creative']);
  assert.deepEqual(defaultModes('4.5', env), ['solo', 'duo', 'squad', '50v50', 'playground']);
});

test('saved choices are filtered to the build, kept in lobby order, and never leave the lobby empty', () => {
  const builds = { '7.40': ['creative', 'SOLO', 'arenasolo', 'stw'], '8.50': ['creative'], '9.40': [] };
  assert.deepEqual(enabledModes('7.40', { builds, env }), ['solo', 'creative', 'stw']);
  assert.deepEqual(enabledModes('8.50', { builds, env }), ['solo', 'creative']);
  assert.deepEqual(enabledModes('9.40', { builds, env }), ['solo']);
  assert.equal(modeEnabled('7.40', 'duo', { builds, env }), false);
  assert.equal(modeEnabled('7.40', 'creative', { builds, env }), true);
  assert.equal(modeEnabled('11.00', 'intro', { builds: { '11.00': ['solo'] }, env }), true);
  assert.equal(modeEnabled('19.00', 'solo', { builds, env }), true);
});

test('Save the World access follows the season switch, and the Dev Kit as before', () => {
  const ua = 'Fortnite/++Fortnite+Release-7.40-CL-5046157 Windows/10';
  assert.equal(stwEnabledFor(ua, { builds: {}, env }), false);
  assert.equal(stwEnabledFor(ua, { builds: { '7.40': ['solo', 'stw'] }, env }), true);
  assert.equal(stwEnabledFor(ua, { builds: {}, env: { TILTED_DEVKIT: '1' } }), true);
});

test('the lobby lists the saved modes and opens the Creative and Save the World buttons', async () => {
  const file = fileURLToPath(new URL('../../../../Runtime/Server/season-modes.json', import.meta.url));
  const made = !fs.existsSync(file);
  if (!made) return; // never overwrite a real install's choices
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify({ version: 1, builds: { '7.40': ['solo', 'squad', 'creative', 'stw'], '4.5': ['solo', 'playground'] } }));
  try {
    const { cloudTextForClient } = await import('../syndicateLegacy111Lobby.js');
    const hotfixes = { defaultgame: '[/Script/FortniteGame.FortTextHotfixConfig]\n+TextReplacements=(Category=Game, Key="F1B39B41400513DFDBBC39BE41A2AA7A", NativeString="Save The\r\nWorld", LocalizedStrings=(("en","UNAVAILABLE")))\n+TextReplacements=(Category=Game, Key="85CC70F746437FF312DF528927FD1F52", NativeString="Creative", LocalizedStrings=(("en","UNAVAILABLE")))\n+TextReplacements=(Category=Game, Key="KEEP", NativeString="PLAY", LocalizedStrings=(("en","Syndicate")))\n[/Script/FortniteGame.FortGameInstance]\n+FrontEndPlaylistData=(PlaylistName=Playlist_DefaultSolo)\n' };
    const g = cloudTextForClient('DefaultGame.ini', 'Fortnite/++Fortnite+Release-7.40-CL-1 Windows/10', hotfixes);
    assert.match(g, /PlaylistName=Playlist_DefaultSolo,PlaylistAccess=\(bEnabled=true,bIsDefaultPlaylist=true/);
    assert.match(g, /PlaylistName=Playlist_DefaultSquad,.*DisplayPriority=1\)/);
    assert.match(g, /PlaylistName=Playlist_PlaygroundV2,.*bIsTournament=false/);
    assert.doesNotMatch(g, /Playlist_DefaultDuo/);
    assert.doesNotMatch(g, /UNAVAILABLE|World"/);
    assert.match(g, /Key="KEEP"/);
    const s4 = cloudTextForClient('DefaultGame.ini', 'Fortnite/++Fortnite+Release-4.5-CL-4159770 Windows/10', hotfixes);
    assert.match(s4, /PlaylistOverrides=\(PlaylistName=Playlist_DefaultDuo,bEnabled=false\)/);
    assert.match(s4, /PlaylistOverrides=\(PlaylistName=Playlist_50v50,bEnabled=false\)/);
    assert.match(s4, /PlaylistOverrides=\(PlaylistName=Playlist_Playground,bEnabled=true\)/);
    assert.doesNotMatch(s4, /Playlist_DefaultSolo,bEnabled/);
  } finally {
    fs.rmSync(file, { force: true });
    try { fs.rmdirSync(fileURLToPath(new URL('../../../../Runtime/Server/', import.meta.url))); fs.rmdirSync(fileURLToPath(new URL('../../../../Runtime/', import.meta.url))); } catch {}
  }
});

test('lobby layout: order, default, locked tiles and badges', () => {
  const builds = { '10.40': ['solo', 'duo', 'arenasolo', 'creative', 'stw'] };
  // No layout: the old list, first one default, no badges.
  assert.deepEqual(lobbyTiles('10.40', { builds, layouts: {}, env }).map(t => [t.mode, t.enabled, t.isDefault, t.badge]),
    [['solo', true, true, ''], ['duo', true, false, ''], ['arenasolo', true, false, ''], ['creative', true, false, '']]);
  const layouts = { '10.40': { order: ['creative', 'arenasolo', 'squad'], default: 'arenasolo', locked: ['squad', 'duo'],
    badges: { creative: 'new', arenasolo: 'LIMITED', solo: 'bogus' } } };
  const tiles = lobbyTiles('10.40', { builds, layouts, env });
  // duo is on, so "locked" is ignored for it; squad is off and shown locked; Save the World is never a tile.
  assert.deepEqual(tiles.map(t => [t.mode, t.enabled, t.isDefault, t.badge]),
    [['creative', true, false, 'new'], ['arenasolo', true, true, 'limited'], ['squad', false, false, ''], ['solo', true, false, ''], ['duo', true, false, '']]);
  // A default that is off or locked falls back to the first tile that is on.
  const fallback = lobbyTiles('10.40', { builds, layouts: { '10.40': { order: ['squad', 'duo'], default: 'squad', locked: ['squad'] } }, env });
  assert.equal(fallback.find(t => t.isDefault).mode, 'duo');
});

test('lobby rows: tiles sit in the mode selector rows, top row first', () => {
  const builds = { '10.40': ['solo', 'duo', 'arenasolo', 'creative'], '3.5.1': ['solo', 'duo', 'squad'] };
  assert.equal(lobbyRowCount('10.40'), 3);
  assert.equal(lobbyRowCount('7.40'), 3);
  assert.equal(lobbyRowCount('3.5.1'), 2);
  assert.equal(lobbyRowCount('4.5'), 1);
  assert.equal(lobbyRowCount('99.99'), 0);
  // No rows saved: every tile in the top row, as before.
  assert.deepEqual(lobbyTiles('10.40', { builds, layouts: {}, env }).map(t => t.row), [0, 0, 0, 0]);
  const layouts = {
    '10.40': { order: ['creative', 'solo', 'arenasolo', 'duo'], rows: { Creative: 1, arenasolo: 2, duo: '1', solo: -3 } },
    '3.5.1': { order: ['squad', 'solo', 'duo'], rows: { squad: 2, duo: 1 } },
  };
  // Sorted by row, then by the saved order; the default falls back to the first tile of the top row.
  assert.deepEqual(lobbyTiles('10.40', { builds, layouts, env }).map(t => [t.mode, t.row, t.isDefault]),
    [['solo', 0, true], ['creative', 1, false], ['duo', 1, false], ['arenasolo', 2, false]]);
  // A season with two rows puts a third-row tile in its last row.
  assert.deepEqual(lobbyTiles('3.5.1', { builds, layouts, env }).map(t => [t.mode, t.row]), [['solo', 0], ['squad', 1], ['duo', 1]]);
});
