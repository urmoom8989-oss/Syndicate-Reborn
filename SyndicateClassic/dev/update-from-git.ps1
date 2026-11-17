# UPDATE FROM GIT: puts the source from this GitHub clone into your Syndicate folder and
# rebuilds what changed. Pull in GitHub Desktop first, then run "UPDATE FROM GIT.cmd".
#   * backend files are copied in (and into Launcher\components, so the launcher keeps them)
#   * the launcher is rebuilt when launcher files changed, or when release-notes.json's version differs from
#     the installed launcher (so a version bump re-stamps it) (needs the .NET 8 SDK)
#   * the game server is rebuilt when game-server files changed (needs Visual Studio 2022 with C++)
# Your accounts, settings, .env and Fortnite builds are never touched.
# -Force rebuilds everything even if nothing changed. -Root picks the Syndicate folder.
param([switch]$Force, [string]$Root = "")
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$repo = Split-Path $PSScriptRoot -Parent

if (Get-Process SyndicateLauncher -ErrorAction SilentlyContinue) { throw 'Close Syndicate Launcher (and Fortnite) first, then run this again.' }
$root = Find-SyndicateRoot $Root
Write-Host "Syndicate folder: $root"
Write-Host "Source (GitHub clone): $repo"
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$summary = New-Object System.Collections.Generic.List[string]

# ---------------------------------------------------------------- backend
Step 'Backend'
$backendSrc = Join-Path $repo 'backend'
$manifest = Join-Path $root 'Launcher\components\manifest.json'
$versionFile = Join-Path $root 'Runtime\components-version.txt'
$text = $null; $entries = @()
# No @() around ConvertFrom-Json: in PowerShell 5.1 that would wrap the whole array as one item.
if (Test-Path $manifest) { $text = [IO.File]::ReadAllText($manifest); $entries = $text | ConvertFrom-Json }
$backup = Join-Path $root "Runtime\Backups\git-update-$stamp"
# The folder on disk, and the prefix the launcher's component manifest uses (they can differ until the launcher has renamed both).
$backendName = Get-BackendFolderName $root
$manifestPrefix = if ($text -and $text.Contains('"Syndicate-backend/')) { 'Syndicate-backend/' } elseif ($text) { 'Helix-backend-main/' } else { 'Syndicate-backend/' }
$changed = 0; $packagesChanged = $false
foreach ($file in (Get-ChildItem -LiteralPath $backendSrc -Recurse -File)) {
  $relative = $file.FullName.Substring($backendSrc.Length + 1)
  $lower = $relative.ToLowerInvariant()
  # Never copy secrets or personal data, even if one ever lands in the repo.
  if ($file.Name -in @('.env', 'jwt.secret', 'tokens.json') -or $lower -match '(^|\\)(node_modules|clientsettings)\\' -or $lower -eq 'src\local\tiltedpartybots.json') { continue }
  $rel = $manifestPrefix + ($relative -replace '\\', '/')
  $dst = Join-Path $root ($backendName + '\' + $relative)
  $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
  if ((Test-Path -LiteralPath $dst) -and (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash.ToUpperInvariant() -eq $hash) { continue }
  if (Test-Path -LiteralPath $dst) {
    $b = Join-Path $backup ($backendName + '\' + $relative)
    New-Item -ItemType Directory -Path (Split-Path $b) -Force | Out-Null
    Copy-Item -LiteralPath $dst -Destination $b -Force
  }
  New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
  Copy-Item -LiteralPath $file.FullName -Destination $dst -Force
  # The launcher restores files listed in its component manifest; give it the new copy and hash.
  $entry = $entries | Where-Object { $_.Path -eq $rel } | Select-Object -First 1
  if ($entry) {
    $old = [string]$entry.Sha256
    if (([regex]::Matches($text, [regex]::Escape($old))).Count -eq 1) {
      $component = Join-Path $root ('Launcher\components\' + ($rel -replace '/', '\'))
      New-Item -ItemType Directory -Path (Split-Path $component) -Force | Out-Null
      Copy-Item -LiteralPath $file.FullName -Destination $component -Force
      $text = $text.Replace($old, $hash)
    } else { Write-Host "  note: manifest entry for $rel is shared with another file; left as is" -ForegroundColor Yellow }
  }
  if ($file.Name -in @('package.json', 'package-lock.json')) { $packagesChanged = $true }
  Write-Host "  updated $rel"
  $changed++
}
if ($changed -gt 0 -and $text) {
  [IO.File]::WriteAllText($manifest, $text, (New-Object Text.UTF8Encoding($false)))
  [IO.File]::WriteAllText($versionFile, (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToUpperInvariant(), (New-Object Text.UTF8Encoding($false)))
}
if ($changed -eq 0) { Write-Host '  already up to date'; $summary.Add('Backend: already up to date') }
else { $summary.Add("Backend: $changed file(s) updated (old copies in Runtime\Backups\git-update-$stamp)") }
if ($packagesChanged) { $summary.Add("Backend: package.json changed - run 'npm install' in $backendName") }

# ---------------------------------------------------------------- launcher
Step 'Launcher'
$launcherSrc = Join-Path $repo 'launcher\launcher-source'
$launcherScript = Join-Path $repo 'build-scripts\build-launcher.ps1'
$launcherHash = Get-TreeHash @($launcherSrc, $launcherScript)
# The launcher bakes in the newest version from release-notes.json when it compiles. Also rebuild when that
# version differs from the installed launcher, so a version bump alone re-stamps it (no -Force needed).
$launcherVersion = ''
try { $launcherVersion = "$((Get-Content -LiteralPath (Join-Path $repo 'public-release\release-notes.json') -Raw | ConvertFrom-Json)[0].version)".Trim() } catch { }
if ($launcherVersion -notmatch '^\d+\.\d+\.\d+$') { $launcherVersion = '' }
$installedDll = Join-Path $root 'Launcher\SyndicateLauncher.dll'
$installedVersion = ''
if (Test-Path $installedDll) { try { $installedVersion = [Reflection.AssemblyName]::GetAssemblyName($installedDll).Version.ToString(3) } catch { } }
$versionMismatch = ($launcherVersion -ne '' -and $installedVersion -ne '' -and $launcherVersion -ne $installedVersion)
if (!$Force -and !$versionMismatch -and $launcherHash -eq (Get-DevState 'launcher') -and (Test-Path $installedDll)) {
  Write-Host '  no launcher changes since the last build'
  $summary.Add('Launcher: no changes')
} elseif (!(Get-Command dotnet -ErrorAction SilentlyContinue)) {
  Write-Host '  skipped: install the .NET 8 SDK (https://dotnet.microsoft.com/download/dotnet/8.0) to build the launcher' -ForegroundColor Yellow
  $summary.Add('Launcher: SKIPPED - .NET 8 SDK not installed')
} else {
  # Same layout as the Desktop "3 - Launcher fixes" package, built from the clone.
  $pkg = Join-Path $SyndicateDevState 'launcher-package'
  if (Test-Path $pkg) { Remove-Item -LiteralPath $pkg -Recurse -Force }
  New-Item -ItemType Directory -Path (Join-Path $pkg 'source-files') -Force | Out-Null
  Copy-Item -Path (Join-Path $launcherSrc 'SyndicateClassic\*.cs') -Destination (Join-Path $pkg 'source-files') -Force
  foreach ($folder in 'season-art', 'avatars', 'pack-art', 'controller-icons', 'pad-assist', 'music') {
    if (Test-Path (Join-Path $launcherSrc $folder)) { Copy-Item -LiteralPath (Join-Path $launcherSrc $folder) -Destination $pkg -Recurse -Force }
  }
  foreach ($single in 'logo.png', 'InstallUpdate.ps1', 'SyndicateClassic.ico') {
    if (Test-Path (Join-Path $launcherSrc $single)) { Copy-Item -LiteralPath (Join-Path $launcherSrc $single) -Destination $pkg -Force }
  }
  Copy-Item -LiteralPath $launcherScript -Destination $pkg -Force
  # $launcherVersion (the newest release-notes.json version) was read above when deciding whether to rebuild.
  if ($versionMismatch) { Write-Host "  version changed ($installedVersion -> $launcherVersion); re-stamping the launcher" }
  & (Join-Path $pkg 'build-launcher.ps1') -Root $root -Version $launcherVersion
  Set-DevState 'launcher' $launcherHash
  $summary.Add('Launcher: rebuilt')
}

# ---------------------------------------------------------------- game server
Step 'Game server'
$serverSrc = Join-Path $repo 'game-server'
$serverScript = Join-Path $repo 'build-scripts\build-erbium.ps1'
$serverHash = Get-TreeHash @($serverSrc, $serverScript)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!$Force -and $serverHash -eq (Get-DevState 'gameserver')) {
  Write-Host '  no game server changes since the last build'
  $summary.Add('Game server: no changes')
} elseif (!(Test-Path $vswhere)) {
  Write-Host '  skipped: install Visual Studio 2022 with "Desktop development with C++" to build the game server.' -ForegroundColor Yellow
  Write-Host '  Until then you keep the game server you already have (from the dev kit).' -ForegroundColor Yellow
  $summary.Add('Game server: SKIPPED - Visual Studio 2022 (C++) not installed; using the current one')
} else {
  # Same layout as the Desktop "2 - Game server fixes" package: the clone's files go over Source\Erbium.
  $pkg = Join-Path $SyndicateDevState 'gameserver-package'
  foreach ($old in 'source-files', 'sdk-files', 'client-files', 'studio-files') { if (Test-Path (Join-Path $pkg $old)) { Remove-Item -LiteralPath (Join-Path $pkg $old) -Recurse -Force } }
  New-Item -ItemType Directory -Path (Join-Path $pkg 'source-files'), (Join-Path $pkg 'sdk-files') -Force | Out-Null
  Copy-Item -Path (Join-Path $serverSrc 'Erbium\*') -Destination (Join-Path $pkg 'source-files') -Recurse -Force
  Copy-Item -Path (Join-Path $serverSrc 'SDK\*') -Destination (Join-Path $pkg 'sdk-files') -Recurse -Force
  New-Item -ItemType Directory -Path (Join-Path $pkg 'client-files') -Force | Out-Null
  Copy-Item -Path (Join-Path $serverSrc 'ErbiumClient\*') -Destination (Join-Path $pkg 'client-files') -Recurse -Force
  # TiltedStudio.dll (the Studio's pipe bridge): without this copy build-erbium.ps1 found no source and skipped it
  # with only a yellow warning, so the Studio kept running an old DLL.
  if (Test-Path (Join-Path $serverSrc 'SyndicateStudio')) {
    New-Item -ItemType Directory -Path (Join-Path $pkg 'studio-files') -Force | Out-Null
    Copy-Item -Path (Join-Path $serverSrc 'SyndicateStudio\*') -Destination (Join-Path $pkg 'studio-files') -Recurse -Force
  }
  Copy-Item -LiteralPath $serverScript -Destination $pkg -Force
  Write-Host "  build log: $(Join-Path $pkg 'last-build-log.txt')"
  & (Join-Path $pkg 'build-erbium.ps1') -Root $root
  Set-DevState 'gameserver' $serverHash
  $summary.Add('Game server: rebuilt')
}

Write-Host ''
Write-Host 'Done.' -ForegroundColor Green
foreach ($line in $summary) { Write-Host "  $line" }
Write-Host ''
Write-Host 'Open Syndicate from Launcher\SyndicateLauncher.exe in your Syndicate folder.'
