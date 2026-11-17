# Installs the 2026-10-22 backend fixes into your Syndicate install:
#  * keybinds/settings are saved in %LOCALAPPDATA%\ProjectTilted\UserData\ClientSettings
#    (kept across versions and shared by every Syndicate copy on this PC)
#  * matchmaking waits longer for a slow first game-server start
#  * your saved outfit is refreshed right before every local match
#  * Seasons 6.20, 7.40, 8.50, 9.40 and 10.31 are accepted (build detection, matchmaking, season progress, lobby playlists)
#  * Battle Pass progress works for Seasons 6-X, and a progress error can no longer crash the backend
# Also updates Launcher\components so the launcher does not undo the change. Close Syndicate first.
$ErrorActionPreference = 'Stop'
$fixes = $PSScriptRoot
function Find-SyndicateRoot {
  $candidates = @(
    (Join-Path $env:USERPROFILE 'Desktop\fortnite syndicate\Syndicate'),
    (Join-Path $env:LOCALAPPDATA 'Project Tilted')
  )
  foreach ($c in $candidates) {
    if ($c -and ((Test-Path (Join-Path $c 'Syndicate-backend\src\index.js')) -or (Test-Path (Join-Path $c 'Helix-backend-main\src\index.js'))) -and (Test-Path (Join-Path $c 'Launcher'))) { return (Resolve-Path $c).Path }
  }
  Add-Type -AssemblyName System.Windows.Forms
  $d = New-Object System.Windows.Forms.FolderBrowserDialog
  $d.Description = 'Select your "Project Tilted" folder (with Syndicate-backend and Launcher)'
  if ($d.ShowDialog() -ne 'OK') { throw 'No Syndicate folder selected.' }
  return $d.SelectedPath
}
if (Get-Process SyndicateLauncher -ErrorAction SilentlyContinue) { throw 'Close Syndicate Launcher (and Fortnite) first, then run this again.' }
$root = Find-SyndicateRoot
Write-Host "Using Syndicate folder: $root"
$files = @(
  'Helix-backend-main/src/Utils/syndicateClientSettings.js',
  'Helix-backend-main/src/Routes/cloudstorage.js',
  'Helix-backend-main/src/Routes/matchmaking.js',
  'Helix-backend-main/src/Routes/Fortnite.js',
  'Helix-backend-main/src/Routes/events.js',
  'Helix-backend-main/src/Routes/launcher.js',
  'Helix-backend-main/src/Routes/timeline.js',
  'Helix-backend-main/src/local/tiltedEventFlags.json',
  'Helix-backend-main/src/Routes/auth.js',
  'Helix-backend-main/src/Utils/syndicateTravel.js',
  'Helix-backend-main/src/Utils/syndicateBuild.js',
  'Helix-backend-main/src/Utils/syndicateSeasonState.js',
  'Helix-backend-main/src/Utils/syndicateLegacy111Lobby.js',
  'Helix-backend-main/src/Utils/syndicateProgression.js',
  'Helix-backend-main/src/Utils/syndicateProgressionCore.js',
  'Helix-backend-main/src/Utils/syndicateArena.js',
  'Helix-backend-main/src/Utils/syndicatePassShop.js',
  'Helix-backend-main/src/Utils/syndicateVictoryRewards.js',
  'Helix-backend-main/src/Utils/syndicateTierPurchase.js',
  'Helix-backend-main/src/Utils/syndicateSeasonChallenges.js',
  'Helix-backend-main/src/Utils/syndicateUnlockAll.js',
  'Helix-backend-main/src/local/Cosmetics/all-cosmetics.json',
  'Helix-backend-main/src/Utils/syndicateLegacy111Profile.js',
  'Helix-backend-main/src/Utils/Utils.js',
  'Helix-backend-main/Matchmaker/playlists.cjs',
  'Helix-backend-main/Matchmaker/index.js',
  'Helix-backend-main/src/Routes/user.js',
  'Helix-backend-main/src/Routes/friends.js',
  'Helix-backend-main/src/Utils/syndicatePartyBots.js',
  'Helix-backend-main/src/Utils/syndicateLegacyImport.js',
  'Helix-backend-main/src/Utils/syndicateShopRotation.js',
  'Helix-backend-main/src/Utils/syndicateLegacyXmpp.js'
)
foreach ($f in $files) { if (!(Test-Path (Join-Path $fixes ('source-files\' + ($f -replace '/', '\'))))) { throw "Missing $f in this folder. Nothing was changed." } }
$manifest = Join-Path $root 'Launcher\components\manifest.json'
$version  = Join-Path $root 'Runtime\components-version.txt'
$stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $root "Runtime\Backups\backend-before-$stamp"
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$text = $null; $entries = @()
if (Test-Path $manifest) { $text = [IO.File]::ReadAllText($manifest); $entries = $text | ConvertFrom-Json; Copy-Item $manifest (Join-Path $backup 'manifest.json') -Force }
if (Test-Path $version) { Copy-Item $version (Join-Path $backup 'components-version.txt') -Force }
# Check every manifest entry first so nothing is half-installed.
foreach ($pkgRel in $files) {
  $rel = $pkgRel; if ($text -and $text.Contains('"Syndicate-backend/')) { $rel = 'Syndicate-backend/' + $pkgRel.Substring('Helix-backend-main/'.Length) }
  $entry = $entries | Where-Object { $_.Path -eq $rel } | Select-Object -First 1
  if ($entry -and ([regex]::Matches($text, [regex]::Escape([string]$entry.Sha256))).Count -ne 1) { throw "The update manifest entry for $rel is ambiguous. Nothing was changed." }
}
# Installs renamed by the launcher have Syndicate-backend; the package and old manifests say Helix-backend-main.
$backendName = if ((Test-Path (Join-Path $root 'Syndicate-backend')) -or !(Test-Path (Join-Path $root 'Helix-backend-main'))) { 'Syndicate-backend' } else { 'Helix-backend-main' }
$manifestPrefix = if ($text -and $text.Contains('"Syndicate-backend/')) { 'Syndicate-backend/' } else { 'Helix-backend-main/' }
foreach ($pkgRel in $files) {
  $rel = $manifestPrefix + $pkgRel.Substring('Helix-backend-main/'.Length)
  $src = Join-Path $fixes ('source-files\' + ($pkgRel -replace '/', '\'))
  $dst = Join-Path $root (($backendName + '/' + $pkgRel.Substring('Helix-backend-main/'.Length)) -replace '/', '\')
  if (Test-Path $dst) { $b = Join-Path $backup ($rel -replace '/', '\'); New-Item -ItemType Directory -Path (Split-Path $b) -Force | Out-Null; Copy-Item $dst $b -Force }
  New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
  Copy-Item $src $dst -Force
  $entry = $entries | Where-Object { $_.Path -eq $rel } | Select-Object -First 1
  if ($entry) {
    $component = Join-Path $root ('Launcher\components\' + ($rel -replace '/', '\'))
    New-Item -ItemType Directory -Path (Split-Path $component) -Force | Out-Null
    Copy-Item $src $component -Force
    $text = $text.Replace([string]$entry.Sha256, (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash.ToUpperInvariant())
  }
  Write-Host "  installed $rel"
}
if ($text) {
  [IO.File]::WriteAllText($manifest, $text, (New-Object Text.UTF8Encoding($false)))
  [IO.File]::WriteAllText($version, (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToUpperInvariant(), (New-Object Text.UTF8Encoding($false)))
}
Write-Host ''
Write-Host 'Done. Backend fixes installed.' -ForegroundColor Green
Write-Host "Previous files saved in: $backup"
