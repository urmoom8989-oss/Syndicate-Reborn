// node --test src/Utils/test/syndicateSeasonModes.test.js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { availableModes, defaultModes, enabledModes, modeEnabled, stwEnabledFor } from '../syndicateSeasonModes.js';

const env = { TILTED_DEVKIT: '' };

test('Creative is offered from Season 7, Arena from Season 8, 50v50 and Playground on Season 4', () => {
  assert.equal(availableModes('6.20').includes('creative'), false);
  for (const b of ['7.40', '8.50', '9.40', '10.40', '11.00', '12.50']) assert.ok(availableModes(b).includes('creative'), b);
  assert.equal(availableModes('7.40').includes('arenasolo'), false);
  assert.ok(availableModes('8.50').includes('arenasolo'));
  assert.deepEqual(availableModes('4.5'), ['solo', 'duo', 'squad', '50v50', 'playground', 'stw']);
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
