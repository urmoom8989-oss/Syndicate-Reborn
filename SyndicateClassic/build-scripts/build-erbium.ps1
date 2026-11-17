# Builds the fixed Erbium game server and installs it so the launcher uses it
# for the next match. Requires Visual Studio 2022 with "Desktop development with C++".
# -Root: the "Project Tilted" folder (UPDATE FROM GIT passes it so nobody has to pick it).
param([string]$Root = "")
$ErrorActionPreference = 'Stop'
$fixes = $PSScriptRoot
# Find the Syndicate install (this folder can live anywhere, e.g. the Desktop).
function Find-SyndicateRoot {
  if ($Root -and ((Test-Path (Join-Path $Root 'Syndicate-backend\src\index.js')) -or (Test-Path (Join-Path $Root 'Helix-backend-main\src\index.js')))) { return (Resolve-Path $Root).Path }
  $candidates = @(
    (Join-Path $PSScriptRoot '..\..'),
    (Join-Path $env:USERPROFILE 'Desktop\fortnite syndicate\Syndicate'),
    (Join-Path $env:LOCALAPPDATA 'Project Tilted')
  )
  foreach ($c in $candidates) {
    if ($c -and ((Test-Path (Join-Path $c 'Syndicate-backend\src\index.js')) -or (Test-Path (Join-Path $c 'Helix-backend-main\src\index.js'))) -and (Test-Path (Join-Path $c 'Source'))) { return (Resolve-Path $c).Path }
  }
  Add-Type -AssemblyName System.Windows.Forms
  $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
  $dialog.Description = 'Select your "Project Tilted" folder (the one containing Syndicate-backend, Launcher and Source)'
  if ($dialog.ShowDialog() -ne 'OK') { throw 'No Syndicate folder selected.' }
  return $dialog.SelectedPath
}
$root  = Find-SyndicateRoot
Write-Host "Using Syndicate folder: $root"
$src   = Join-Path $root 'Source\Erbium'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$work  = Join-Path $env:TEMP "SyndicateErbiumBuild-$stamp"

function Step($text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Cyan }

# The repo holds the whole game server (Erbium, SDK, ErbiumClient), so Source\Erbium is optional:
# a fresh PC (or a dev kit without Source) builds from the clone's files alone.
if (Test-Path -LiteralPath $src) {
  Step 'Copying Source\Erbium to a temporary build folder'
  robocopy $src $work /MIR /XD x64 .vs /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "Copy failed (robocopy code $LASTEXITCODE)." }
} else {
  Step 'No Source\Erbium folder: building from the repo files only'
  New-Item -ItemType Directory -Path (Join-Path $work 'Erbium'), (Join-Path $work 'SDK') -Force | Out-Null
}

# When run from the git repo (build-scripts/ has no source-files subfolder) the repo's
# game-server/ already has the merged files, so copy those into the build folder instead.
$repoGameServer = Join-Path $fixes '..\game-server'
if (Test-Path (Join-Path $fixes 'source-files')) {
  Step 'Applying the 2026-10-21 fixes (newest candidate files + glider/marker/knock/stall fixes)'
  Copy-Item -Path (Join-Path $fixes 'source-files\*') -Destination (Join-Path $work 'Erbium') -Recurse -Force
} elseif (Test-Path $repoGameServer) {
  Step 'Copying game-server files from the git repo'
  Copy-Item -Path (Join-Path $repoGameServer 'Erbium\*') -Destination (Join-Path $work 'Erbium') -Recurse -Force
  if (Test-Path (Join-Path $repoGameServer 'SDK')) { Copy-Item -Path (Join-Path $repoGameServer 'SDK\*') -Destination (Join-Path $work 'SDK') -Recurse -Force }
} else {
  throw 'No source files found: run from an Updated Files package or the git repo.'
}
# SDK fixes (Core.h: safe object lookup during server startup).
if (Test-Path (Join-Path $fixes 'sdk-files')) { Copy-Item -Path (Join-Path $fixes 'sdk-files\*') -Destination (Join-Path $work 'SDK') -Recurse -Force }
# Client-side modules (the custom FOV module SyndicateFov.dll is built from here, after the game server).
if (Test-Path (Join-Path $fixes 'client-files')) {
  New-Item -ItemType Directory -Path (Join-Path $work 'ErbiumClient') -Force | Out-Null
  Copy-Item -Path (Join-Path $fixes 'client-files\*') -Destination (Join-Path $work 'ErbiumClient') -Recurse -Force
}

Step 'Finding Visual Studio'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path $vswhere)) { throw 'Visual Studio 2022 is not installed. Install it with "Desktop development with C++", then run this again.' }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'MSBuild was not found. Add the "Desktop development with C++" workload in Visual Studio Installer.' }

Step 'Building Erbium (Release x64)'
# Also save the full output next to this script so build errors can be read without copying them.
$buildLog = Join-Path $fixes 'last-build-log.txt'
& $msbuild (Join-Path $work 'Erbium\Erbium.vcxproj') /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$work\" /m /v:minimal /fl "/flp:LogFile=$buildLog;Verbosity=normal"
if ($LASTEXITCODE) { throw "Build failed - see the errors above. Nothing was installed. Build folder: $work" }
$dll = Get-ChildItem -Path $work -Recurse -Filter 'Erbium.dll' | Where-Object { $_.FullName -match '\\x64\\Release\\' } |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$dll) { throw "Build reported success but Erbium.dll was not found under $work." }
# Keep this build's debug symbols so crash addresses in the server log can be read.
$symbols = Join-Path $fixes 'symbols'
New-Item -ItemType Directory -Path $symbols -Force | Out-Null
Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $symbols 'Erbium.dll') -Force
$pdb = [IO.Path]::ChangeExtension($dll.FullName, '.pdb')
if (Test-Path -LiteralPath $pdb) { Copy-Item -LiteralPath $pdb -Destination (Join-Path $symbols 'Erbium.pdb') -Force }

Step 'Installing (the launcher swaps it in when the next match server starts)'
$rel      = 'Erbium-Quality-of-life-changes-main/Erbium/x64/Release/Erbium.dll.pending'
$pending  = Join-Path $root ($rel -replace '/', '\')
$component= Join-Path $root ('Launcher\components\' + ($rel -replace '/', '\'))
$manifest = Join-Path $root 'Launcher\components\manifest.json'
$version  = Join-Path $root 'Runtime\components-version.txt'
$backup   = Join-Path $root "Runtime\Backups\erbium-before-$stamp"
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach ($f in @($pending, $component, $manifest, $version, ($pending -replace '\.pending$', ''))) {
  if (Test-Path -LiteralPath $f) { Copy-Item -LiteralPath $f -Destination (Join-Path $backup ([IO.Path]::GetFileName($f))) -Force }
}
$newHash = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
$text = [IO.File]::ReadAllText($manifest)
$entries = $text | ConvertFrom-Json
$entry = $entries | Where-Object { $_.Path -eq $rel } | Select-Object -First 1
if (!$entry) { throw "The component manifest has no entry for $rel. Nothing was installed." }
$text = $text.Replace($entry.Sha256, $newHash)
Copy-Item -LiteralPath $dll.FullName -Destination $pending -Force
New-Item -ItemType Directory -Path (Split-Path $component) -Force | Out-Null
Copy-Item -LiteralPath $dll.FullName -Destination $component -Force
[IO.File]::WriteAllText($manifest, $text, (New-Object Text.UTF8Encoding($false)))
$manifestHash = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToUpperInvariant()
[IO.File]::WriteAllText($version, $manifestHash, (New-Object Text.UTF8Encoding($false)))

Write-Host ''
Write-Host "Done. New Erbium.dll installed (SHA256 $newHash)." -ForegroundColor Green
Write-Host "Previous files saved in: $backup"
Write-Host 'Start a new match from the launcher to use it.'

# Client mods (TiltedClient.dll: custom FOV, turbo build), loaded into Fortnite by the launcher when one of
# them is turned on. A failure here never undoes the game server installed above.
Step 'Building the client mods (TiltedClient.dll)'
try {
  $clientProject = Join-Path $work 'ErbiumClient\SyndicateClient.vcxproj'
  if (!(Test-Path $clientProject)) { throw 'SyndicateClient.vcxproj is not in this source package.' }
  $clientLog = Join-Path $fixes 'last-client-build-log.txt'
  & $msbuild $clientProject /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$work\" /m /v:minimal /fl "/flp:LogFile=$clientLog;Verbosity=normal"
  $clientFailed = $LASTEXITCODE
  # A copy next to the other runtime logs, so a failed client build can be read without opening AppData.
  try { Copy-Item -LiteralPath $clientLog -Destination (Join-Path $root 'Runtime\Server\last-client-build-log.txt') -Force } catch { }
  if ($clientFailed) { throw "build failed, see $clientLog (copy in Runtime\Server\last-client-build-log.txt)" }
  $clientDll = Get-ChildItem -Path $work -Recurse -Filter 'TiltedClient.dll' | Where-Object { $_.FullName -match '\\x64\\Release\\' } |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (!$clientDll) { throw 'the build did not produce TiltedClient.dll' }
  Copy-Item -LiteralPath $clientDll.FullName -Destination (Join-Path $root 'Launcher\TiltedClient.dll') -Force
  # The first version of this module was called SyndicateFov.dll; it is no longer used.
  $oldFov = Join-Path $root 'Launcher\SyndicateFov.dll'
  if (Test-Path -LiteralPath $oldFov) { Remove-Item -LiteralPath $oldFov -Force -ErrorAction SilentlyContinue }
  Write-Host 'Client mods installed (Launcher\TiltedClient.dll).' -ForegroundColor Green
} catch {
  Write-Host "Client mods not updated: $($_.Exception.Message). Close Fortnite if it is running and run this again. The game server above is installed either way." -ForegroundColor Yellow
}

# TiltedStudio.dll (Dev Kit only): injected into the game client by Syndicate Studio for direct UE4 access
# (actor queries, camera, projection, raycasting). Optional — the Studio falls back to file-based mode without it.
Step 'Building TiltedStudio.dll (Dev Kit pipe bridge)'
try {
  $studioProject = Join-Path $work 'SyndicateStudio\SyndicateStudio.vcxproj'
  if (!(Test-Path $studioProject)) {
    # SyndicateStudio may live beside Erbium in the source tree, or in the git repo's game-server/
    $studioSrc = $null
    # Newest first: the package's studio-files (UPDATE FROM GIT), the repo's game-server/, then an old Source copy
    foreach ($candidate in @((Join-Path $fixes 'studio-files'), (Join-Path $fixes '..\game-server\SyndicateStudio'), (Join-Path $root 'Source\SyndicateStudio'))) {
      if (Test-Path $candidate) { $studioSrc = $candidate; break }
    }
    if ($studioSrc) {
      New-Item -ItemType Directory -Path (Join-Path $work 'SyndicateStudio') -Force | Out-Null
      Copy-Item -Path (Join-Path $studioSrc '*') -Destination (Join-Path $work 'SyndicateStudio') -Recurse -Force
      $studioProject = Join-Path $work 'SyndicateStudio\SyndicateStudio.vcxproj'
    }
  }
  if (!(Test-Path $studioProject)) { throw 'SyndicateStudio.vcxproj is not in this source package.' }
  $studioLog = Join-Path $fixes 'last-studio-build-log.txt'
  & $msbuild $studioProject /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$work\" /m /v:minimal /fl "/flp:LogFile=$studioLog;Verbosity=normal"
  $studioFailed = $LASTEXITCODE
  try { Copy-Item -LiteralPath $studioLog -Destination (Join-Path $root 'Runtime\Server\last-studio-build-log.txt') -Force } catch { }
  if ($studioFailed) { throw "build failed, see $studioLog (copy in Runtime\Server\last-studio-build-log.txt)" }
  $studioDll = Get-ChildItem -Path $work -Recurse -Filter 'TiltedStudio.dll' | Where-Object { $_.FullName -match '\\x64\\Release\\' } |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (!$studioDll) { throw 'the build did not produce TiltedStudio.dll' }
  Copy-Item -LiteralPath $studioDll.FullName -Destination (Join-Path $root 'Launcher\TiltedStudio.dll') -Force
  Write-Host 'TiltedStudio.dll installed (Launcher\TiltedStudio.dll).' -ForegroundColor Green
} catch {
  Write-Host "TiltedStudio.dll not built: $($_.Exception.Message). The Studio will fall back to file-based mode." -ForegroundColor Yellow
}
