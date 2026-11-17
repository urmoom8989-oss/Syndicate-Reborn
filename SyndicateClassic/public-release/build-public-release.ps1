# Builds a single encrypted Syndicate EXE you can hand out.
# Run BUILD GAME SERVER and BUILD LAUNCHER first so the release contains your latest fixes.
# Nothing in your install is changed; output goes to Desktop\Project-Syndicate-Public-Release.
# -Private: a test build for a few people. Same EXE, but "Check for updates" is off (testers stay on this
# build instead of jumping to a public release), it goes to Desktop\Project-Syndicate-Private-Release and the
# file name says "private". It does not change the public release numbering (last-version.txt).
# -Tester: the Tester launcher (MAKE TESTER RELEASE.cmd). Only opens for the Tester role in the Syndicate
# Discord (public-release\TesterGate.cs, checked by the relay), updates itself from the relay's newest tester
# build, and is published as a GitHub PRE-release (public players never get it: they only follow full releases).
# Version = the top of release-notes.json; the tester build number (b...) goes up by itself every build.
param([string]$Version = "", [string]$UpdateRepo = "", [switch]$Private, [switch]$NoObfuscate, [switch]$Tester, [string]$Relay = "https://your-relay.example.workers.dev")
$ErrorActionPreference = "Stop"
$env:DOTNET_CLI_DISABLE_BUILD_SERVERS = '1'
$env:DOTNET_NOLOGO = '1'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$fixes = $PSScriptRoot

function Step($t) { Write-Host ""; Write-Host "== $t" -ForegroundColor Cyan }
function Test-ReleaseRoot($c) {
  return ($c -and ((Test-Path (Join-Path $c 'Syndicate-backend\src\index.js')) -or (Test-Path (Join-Path $c 'Helix-backend-main\src\index.js'))) -and (Test-Path (Join-Path $c 'Source\PublicStandalone040\Program.cs')) -and (Test-Path (Join-Path $c 'Source\PublicStandalone040\launcher-source')))
}
function Find-SyndicateRoot {
  # Same folder UPDATE FROM GIT uses (remembered in %LOCALAPPDATA%\ProjectTilted\dev\syndicate-root.txt), then the usual places.
  $candidates = @()
  $saved = Join-Path $env:LOCALAPPDATA 'SyndicateClassic\dev\syndicate-root.txt'
  if (Test-Path $saved) { $candidates += (Get-Content -LiteralPath $saved -Raw).Trim() }
  $candidates += @(
    (Join-Path $env:USERPROFILE 'Desktop\fortnite syndicate\Syndicate'),
    (Join-Path $env:LOCALAPPDATA 'Project Tilted')
  )
  foreach ($c in $candidates) { if (Test-ReleaseRoot $c) { return (Resolve-Path $c).Path } }
  Add-Type -AssemblyName System.Windows.Forms
  $d = New-Object System.Windows.Forms.FolderBrowserDialog
  $d.Description = 'Select your "Project Tilted" folder (with Syndicate-backend, Launcher, Source and Dependencies)'
  if ($d.ShowDialog() -ne 'OK') { throw 'No Syndicate folder selected.' }
  if (!(Test-ReleaseRoot $d.SelectedPath)) { throw "That folder is not a Syndicate folder (it needs Syndicate-backend and Source\PublicStandalone040\launcher-source): $($d.SelectedPath)" }
  return $d.SelectedPath
}

# ---- Pick a .NET 8 SDK -------------------------------------------------------
# The .NET 10 tools are marked to require CET, which this Windows can't honor, so Windows
# kills them. We call a .NET 8 dotnet.exe DIRECTLY (by full path), preferring the copy in
# your user folder, so the .NET 10 one in Program Files is never involved.
function Find-Dotnet8 {
  $exes = @(
    (Join-Path $env:USERPROFILE '.dotnet\dotnet.exe'),
    (Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'),
    (Join-Path $env:ProgramFiles 'dotnet\dotnet.exe')
  )
  $onPath = Get-Command dotnet -ErrorAction SilentlyContinue
  if ($onPath) { $exes += $onPath.Source }
  foreach ($exe in ($exes | Select-Object -Unique)) {
    if (!(Test-Path -LiteralPath $exe)) { continue }
    $sdkDir = Join-Path (Split-Path $exe) 'sdk'
    if (!(Test-Path $sdkDir)) { continue }
    $v8 = Get-ChildItem $sdkDir -Directory -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -match '^8\.0\.\d+$' } |
          Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if ($v8) { return @{ Exe = $exe; Sdk = $v8.Name } }
  }
  return $null
}

$dn = Find-Dotnet8
if (!$dn) {
  Write-Host ""
  Write-Host "No .NET 8 SDK was found on this PC." -ForegroundColor Yellow
  Write-Host "Install 'SDK 8.0 - Windows x64' from https://dotnet.microsoft.com/download/dotnet/8.0"
  Write-Host "then double-click MAKE PUBLIC RELEASE again."
  throw 'A .NET 8 SDK is required.'
}
$dotnet = $dn.Exe
# Keep this dotnet.exe from handing off to the .NET 10 install in Program Files.
$env:DOTNET_ROOT = Split-Path $dotnet
$env:DOTNET_MULTILEVEL_LOOKUP = '0'
$pin = '{ "sdk": { "version": "' + $dn.Sdk + '", "rollForward": "latestPatch" } }'
Write-Host "Using .NET SDK $($dn.Sdk) from $dotnet"

# Run dotnet from inside $dir (that is where it looks for global.json) and confirm it is 8.x.
function Invoke-Dotnet8([string]$dir, [string[]]$dnArgs) {
  Set-Content -LiteralPath (Join-Path $dir 'global.json') -Value $pin -Encoding ASCII
  Push-Location $dir
  try {
    $ver = (& $dotnet --version 2>$null | Select-Object -First 1)
    if ("$ver" -notlike '8.*') { throw "Expected .NET SDK 8.x but got '$ver' in $dir. Nothing was built." }
    & $dotnet @dnArgs | Out-Host
    return $LASTEXITCODE
  } finally { Pop-Location }
}

$testerRepo = ''
if ($Tester) {
  # Tester builds follow the newest version in the release notes and never touch the public numbering.
  $testerRepoFile = Join-Path $fixes 'update-repo.txt'
  if (Test-Path $testerRepoFile) { $testerRepo = (Get-Content -LiteralPath $testerRepoFile -Raw).Trim() }
  if (!$Version) { $Version = "$(((Get-Content -LiteralPath (Join-Path $fixes 'release-notes.json') -Raw) | ConvertFrom-Json)[0].version)".Trim() }
  $Private = $true   # no public "Check for updates": testers update from the relay instead (TesterGate.cs)
  # Minutes since 2026-01-01 UTC: always higher than the build before it.
  $testerBuild = [int][math]::Floor(([DateTime]::UtcNow - [DateTime]::new(2026, 1, 1, 0, 0, 0, [DateTimeKind]::Utc)).TotalMinutes)
  Write-Host "Tester build b$testerBuild (v$Version), relay $Relay" -ForegroundColor Cyan
}
$deliver = Join-Path ([Environment]::GetFolderPath('Desktop')) $(if ($Tester) { 'Project-Syndicate-Tester-Release' } elseif ($Private) { 'Project-Syndicate-Private-Release' } else { 'Project-Syndicate-Public-Release' })
$lastFile = Join-Path $deliver 'last-version.txt'
if (!$Version) {
  # Every update needs a higher number than the last one, or players won't get it.
  $last = if (Test-Path $lastFile) { (Get-Content -LiteralPath $lastFile -Raw).Trim() } else { '0.4.1' }
  $suggest = '0.4.2'
  $parsed = $null
  if ([version]::TryParse($last, [ref]$parsed) -and $parsed.Build -ge 0) { $suggest = "{0}.{1}.{2}" -f $parsed.Major, $parsed.Minor, ($parsed.Build + 1) }
  $answer = Read-Host "Version number for this release (last was $last) [press Enter for $suggest]"
  $Version = if ($answer.Trim()) { $answer.Trim().TrimStart('v','V') } else { $suggest }
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must look like 0.4.2 (got '$Version')." }
$root = Find-SyndicateRoot
if ($Private) { $UpdateRepo = '' }
elseif (!$UpdateRepo) {
  $repoFile = Join-Path $fixes 'update-repo.txt'
  if (Test-Path $repoFile) { $UpdateRepo = (Get-Content -LiteralPath $repoFile -Raw).Trim() }
  elseif (Test-Path (Join-Path $root 'Launcher\updater.json')) { try { $UpdateRepo = ((Get-Content -LiteralPath (Join-Path $root 'Launcher\updater.json') -Raw) | ConvertFrom-Json).repository } catch {} }
}
if ($UpdateRepo -and $UpdateRepo -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw "update-repo.txt must look like owner/repo (got '$UpdateRepo')." }
Write-Host ("Update source (Check for updates button): " + $(if ($UpdateRepo) { "https://github.com/$UpdateRepo/releases" } else { 'none (updates off)' }))
Write-Host "Using Syndicate folder: $root"
$srcProj = Join-Path $root 'Source\PublicStandalone040'
# The repo's copy of the bootstrapper (launcher\Program.cs) is the one we edit together; use it
# when this script runs from a GitHub clone, so a release gets the same changes on both PCs.
$repoProj = Join-Path (Split-Path $fixes -Parent) 'launcher'
if ((Test-Path (Join-Path $repoProj 'Program.cs')) -and (Test-Path (Join-Path $repoProj 'Bundle.csproj'))) { $srcProj = $repoProj }
if (!(Test-Path (Join-Path $fixes 'Updater.cs'))) { throw "Missing Updater.cs in $fixes." }
foreach ($f in 'Program.cs','Bundle.csproj') { if (!(Test-Path (Join-Path $srcProj $f))) { throw "Missing $f in $srcProj (needed for the bootstrapper)." } }

$stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$work   = Join-Path $env:TEMP "SyndicateRelease-$stamp"
$bundle = Join-Path $work 'bundle'
$packer = Join-Path $work 'packer'
New-Item -ItemType Directory -Path $bundle, $packer -Force | Out-Null
Copy-Item (Join-Path $fixes 'Packer\Packer.cs'), (Join-Path $fixes 'Packer\Packer.csproj') $packer

Step 'Preparing the bootstrapper (your validated v0.3.40 bundler)'
Copy-Item (Join-Path $srcProj 'Program.cs') (Join-Path $bundle 'Program.cs')
Copy-Item (Join-Path $srcProj 'Bundle.csproj') (Join-Path $bundle 'Bundle.csproj')
$program = Get-Content -LiteralPath (Join-Path $bundle 'Program.cs') -Raw
$keyFrom = 'Convert.FromBase64String(PayloadKey.Value)'
if ($program -notmatch [regex]::Escape($keyFrom)) { throw 'Could not find the key line in Program.cs; the bootstrapper differs from the expected one. Nothing was built.' }
$program = $program.Replace($keyFrom, 'PayloadKey.Bytes()')
$program = [regex]::Replace($program, 'const string Version="[^"]*"', "const string Version=`"$Version`"", 1)
# Keep the unpacked files between launches. Deleting them on exit meant ~300 MB was
# rewritten every launch, so antivirus rescanned everything each time (freezes, and the
# game server sometimes too slow for Ready Up). Unchanged files are verified, not rewritten.
$cleanupFrom = 'int remaining=Cleanup(zip,root);'
if ($program -notmatch [regex]::Escape($cleanupFrom)) { throw 'Could not find the cleanup line in Program.cs; the bootstrapper differs from the expected one. Nothing was built.' }
$program = $program.Replace($cleanupFrom, 'int remaining=0;')
# Updates: only when the launcher's Check for updates button asks (no automatic check at start).
foreach ($pair in @(
    @('acquired=mutex.WaitOne(0);', 'acquired=mutex.WaitOne(Updater.MutexWaitMs(args));'),
    @('if(!test)ShowProgress();var encrypted=Embedded();', 'if(!test)ShowProgress();if(!test&&Updater.TryUpdate(Version,args,Progress,()=>mutex.ReleaseMutex())){HideProgress();return 0;}var encrypted=Embedded();'),
    @('HideProgress();child.WaitForExit();', 'HideProgress();System.Threading.Tasks.Task.Run(()=>Updater.CleanOldVersions(home,Version));child.WaitForExit();'),
    # The launcher's "Check for updates" button restarts this EXE with --check-updates; tell it where the EXE is.
    @('start.ArgumentList.Add("--skip-update");', 'start.Environment["TILTED_STANDALONE_EXE"]=Environment.ProcessPath??"";start.ArgumentList.Add("--skip-update");'))) {
  if ($program -notmatch [regex]::Escape($pair[0])) { throw "Could not find '$($pair[0])' in Program.cs; the bootstrapper differs from the expected one. Nothing was built." }
  $program = $program.Replace($pair[0], $pair[1])
}
if ($Tester) {
  # The Tester gate runs before anything is unpacked; the tester files live in their own folder.
  foreach ($pair in @(
      @('"SyndicateClassicStandalone",Version', '"SyndicateClassicTester",Version'),
      @('if(!test&&Updater.TryUpdate(Version,args,', 'if(!test&&TesterGate.Run(args,Progress,()=>mutex.ReleaseMutex())!=0){HideProgress();return 0;}if(!test&&Updater.TryUpdate(Version,args.Where(a=>a!="--check-updates").ToArray(),'),
      @('child.WaitForExit();', 'TesterGate.Watch(child);child.WaitForExit();'),
      @('start.Environment["TILTED_STANDALONE_EXE"]=', "start.Environment[`"TILTED_TESTER`"]=`"b$testerBuild`";start.Environment[`"TILTED_STANDALONE_EXE`"]="))) {
    if ($program -notmatch [regex]::Escape($pair[0])) { throw "Could not find '$($pair[0])' in Program.cs for the Tester gate. Nothing was built." }
    $program = $program.Replace($pair[0], $pair[1])
  }
  $gate = (Get-Content -LiteralPath (Join-Path $fixes 'TesterGate.cs') -Raw).Replace('__TILTED_RELAY__', $Relay.TrimEnd('/')).Replace('__TILTED_TESTER_BUILD__', "$testerBuild")
  [IO.File]::WriteAllText((Join-Path $bundle 'TesterGate.cs'), $gate)
}
$updater = Get-Content -LiteralPath (Join-Path $fixes 'Updater.cs') -Raw
[IO.File]::WriteAllText((Join-Path $bundle 'Updater.cs'), $updater.Replace('__TILTED_UPDATE_REPO__', $UpdateRepo))
[IO.File]::WriteAllText((Join-Path $bundle 'Program.cs'), $program)
$proj = Get-Content -LiteralPath (Join-Path $bundle 'Bundle.csproj') -Raw
$proj = [regex]::Replace($proj, '<Version>[^<]*</Version>', "<Version>$Version</Version>", 1)
if ($proj -notmatch [regex]::Escape('<Compile Include="Program.cs"/>')) { throw 'Bundle.csproj differs from the expected one. Nothing was built.' }
$proj = $proj.Replace('<Compile Include="Program.cs"/>', '<Compile Include="Program.cs"/><Compile Include="Updater.cs"/>' + $(if ($Tester) { '<Compile Include="TesterGate.cs"/>' } else { '' }))
# The EXE's icon (Explorer, taskbar, desktop shortcut): the Syndicate logo from launcher-source\SyndicateClassic.ico.
$exeIcon = @((Join-Path $srcProj 'launcher-source\SyndicateClassic.ico'), (Join-Path $fixes 'SyndicateClassic.ico')) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($exeIcon) {
  Copy-Item -LiteralPath $exeIcon -Destination (Join-Path $bundle 'SyndicateClassic.ico') -Force
  $proj = $proj.Replace('</PropertyGroup>', '<ApplicationIcon>SyndicateClassic.ico</ApplicationIcon></PropertyGroup>')
} else { Write-Host 'Note: SyndicateClassic.ico not found; the EXE gets the default icon.' -ForegroundColor Yellow }
[IO.File]::WriteAllText((Join-Path $bundle 'Bundle.csproj'), $proj)

Step 'Building the public launcher (no Developer Tools)'
# The launcher installed in Syndicate\Launcher is a Dev Kit build (Developer Tools on). Build a
# public one from this clone's source, swap it in only while the runtime is packed, then put the Dev
# Kit launcher back, so the install is unchanged afterwards.
$repoRoot = Split-Path $fixes -Parent
$launcherSrc = Join-Path $repoRoot 'launcher\launcher-source'
$launcherScript = Join-Path $repoRoot 'build-scripts\build-launcher.ps1'
if (!(Test-Path (Join-Path $launcherSrc 'ProjectTilted')) -or !(Test-Path $launcherScript)) { throw 'Run this from the GitHub clone (launcher\launcher-source and build-scripts are needed to build the public launcher).' }
$pkg = Join-Path $work 'launcher-package'
New-Item -ItemType Directory -Path (Join-Path $pkg 'source-files') -Force | Out-Null
Copy-Item -Path (Join-Path $launcherSrc 'SyndicateClassic\*.cs') -Destination (Join-Path $pkg 'source-files') -Force
foreach ($folder in 'season-art', 'avatars', 'pack-art', 'controller-icons', 'pad-assist', 'music') { if (Test-Path (Join-Path $launcherSrc $folder)) { Copy-Item -LiteralPath (Join-Path $launcherSrc $folder) -Destination $pkg -Recurse -Force } }
foreach ($single in 'logo.png', 'InstallUpdate.ps1', 'SyndicateClassic.ico') { if (Test-Path (Join-Path $launcherSrc $single)) { Copy-Item -LiteralPath (Join-Path $launcherSrc $single) -Destination $pkg -Force } }
Copy-Item -LiteralPath $launcherScript -Destination $pkg -Force
$publicDll = Join-Path $work 'SyndicateLauncher.public.dll'
& (Join-Path $pkg 'build-launcher.ps1') -Root $root -Public -OutFile $publicDll -Version $Version
if (!(Test-Path $publicDll)) { throw 'The public launcher was not built. Nothing was handed out.' }
# Dev Kit builds contain this marker (MatchTelemetry.BuildKind); a public release must not.
function Test-DevKitDll($path) {
  # .NET stores string constants as UTF-16; check both byte alignments.
  $bytes = [IO.File]::ReadAllBytes($path)
  foreach ($offset in 0, 1) { if ([Text.Encoding]::Unicode.GetString($bytes, $offset, $bytes.Length - $offset).Contains('TILTED_DEVKIT_BUILD')) { return $true } }
  return $false
}
if (Test-DevKitDll $publicDll) { throw 'The public launcher still contains Developer Tools. Nothing was handed out.' }

# ---- Obfuscation ------------------------------------------------------------------------------------------------
# The launcher is already obfuscated by build-launcher.ps1 -Public: that script encrypts every string literal
# (HideStrings) and renames private types/methods (HidePrivateApi) with the same settings used here, THEN self-tests
# that the obfuscated launcher actually opens before handing it back - and it throws (nothing handed out) if Obfuscar
# cannot run. So the DLL we received in $publicDll is guaranteed obfuscated-and-verified, or the build already failed.
# We do NOT re-obfuscate here: running a second, older Obfuscar over the already-processed DLL fails outright
# ("Writing mixed-mode assemblies is not supported") and only ever duplicated work the launcher build already did.
# -NoObfuscate is accepted for backwards compatibility but has no effect; the public launcher is always obfuscated.
Write-Host 'Launcher already obfuscated and self-tested by the public launcher build (strings encrypted, private members renamed).' -ForegroundColor Green

$installedDll = Join-Path $root 'Launcher\SyndicateLauncher.dll'
$devKitDll = Join-Path $work 'SyndicateLauncher.devkit.dll'
if (Test-Path $installedDll) { Copy-Item -LiteralPath $installedDll -Destination $devKitDll -Force }
Copy-Item -LiteralPath $publicDll -Destination $installedDll -Force
Write-Host 'Public launcher ready (Developer Tools are not in it).'

Step 'Scrambling the backend (minify)'
# What a user can extract from the installed launcher is the backend's .js. Minifying it first means the extracted
# copy is one-line, renamed, comment-stripped JavaScript - it still RUNS, but it is not editable source someone can
# rebrand and claim. This is minification/obfuscation, NOT encryption: code that runs on a user's PC can always be
# read with enough effort. Data files (.json, .pem, config) stay external and untouched so the backend can read them.
#
# Safety first: this must never ship a broken backend. We minify each file to a side file, run `node --check` on it,
# and only replace the original when it still parses - otherwise that file stays readable source. If esbuild or Node
# is unavailable, or index.js does not survive, we ship the plain backend. The Packer falls back the same way.
$minBackend = $null
try {
  $beSrc = Join-Path $root 'Syndicate-backend'; if (-not (Test-Path (Join-Path $beSrc 'src\index.js'))) { $beSrc = Join-Path $root 'Helix-backend-main' }
  if (-not (Test-Path (Join-Path $beSrc 'src\index.js'))) { throw "no backend at $beSrc" }

  # Find Node (bundled with the install first, then PATH) and its npm, so build machines without a global Node still work.
  $nodeExe = Join-Path $root 'Dependencies\Node\node.exe'
  if (-not (Test-Path $nodeExe)) { $nodeExe = (Get-Command node -ErrorAction SilentlyContinue).Source }
  if (-not $nodeExe) { throw 'Node not found (looked in Dependencies\Node and PATH)' }
  $npmCli = Join-Path (Split-Path $nodeExe) 'node_modules\npm\bin\npm-cli.js'
  if (-not (Test-Path $npmCli)) { $npmCli = $null }

  # Get esbuild into the work folder (never touches the install or the repo).
  $esbHome = Join-Path $work 'esbuild'; New-Item -ItemType Directory -Force -Path $esbHome | Out-Null
  Write-Host 'Fetching esbuild...'
  if ($npmCli) { & $nodeExe $npmCli install 'esbuild@0.24.2' --no-save --no-audit --no-fund --prefix $esbHome 2>&1 | Out-Host }
  else { & npm install 'esbuild@0.24.2' --no-save --no-audit --no-fund --prefix $esbHome 2>&1 | Out-Host }
  $esbShim = Join-Path $esbHome 'node_modules\esbuild\bin\esbuild'
  if (-not (Test-Path $esbShim)) { throw 'esbuild did not install (no internet / npm blocked?)' }

  # Work on a copy; the install's backend is never modified.
  $minBackendTry = Join-Path $work 'backend-min'
  Copy-Item -LiteralPath $beSrc -Destination $minBackendTry -Recurse -Force
  $files = Get-ChildItem -LiteralPath (Join-Path $minBackendTry 'src') -Recurse -File -Include *.js,*.mjs
  $ok = 0; $kept = 0; $indexOk = $false
  foreach ($f in $files) {
    $tmp = Join-Path $f.DirectoryName ('_min_' + $f.Name)   # same dir so package.json "type":"module" applies to node --check
    try {
      & $nodeExe $esbShim $f.FullName '--minify' '--format=esm' '--platform=node' "--outfile=$tmp" 2>$null
      $checkOk = $false
      if (Test-Path $tmp) { & $nodeExe '--check' $tmp 2>$null; $checkOk = ($LASTEXITCODE -eq 0) }
      if ($checkOk) {
        Move-Item -LiteralPath $tmp -Destination $f.FullName -Force; $ok++
        if ($f.FullName -ieq (Join-Path $minBackendTry 'src\index.js')) { $indexOk = $true }
      } else { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue; $kept++ }
    } catch { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue; $kept++ }
  }
  if (-not $indexOk) { throw "index.js did not survive minification - shipping readable backend instead" }
  Write-Host "Backend scrambled: $ok file(s) minified, $kept left readable (failed check)." -ForegroundColor Green
  $minBackend = $minBackendTry
} catch {
  Write-Warning "Backend minify skipped ($($_.Exception.Message)); shipping the readable backend. The release is still valid."
  $minBackend = $null
}

Step 'Collecting and encrypting the runtime'
# Build tamper-detection: if the release signing key is present, the Packer writes a signed build-provenance.json so
# the launcher can verify it is a genuine, unmodified build (BuildProvenance.cs). The key lives only here and is
# gitignored; without it the build still ships, just unsigned (the launcher then shows no official/unverified state).
$signKey = Join-Path $repoRoot 'public-release\signing\tilted-sign.key'
$packArgs = @('run', '--project', 'Packer.csproj', '-c', 'Release', '-p:UseSharedCompilation=false', '--', '--install', $root, '--out', $bundle, '--version', $Version)
if ($minBackend) { $packArgs += @('--backend', $minBackend) }
if (Test-Path $signKey) { $packArgs += @('--sign-key', $signKey); Write-Host 'Signing the build with the release key.' -ForegroundColor Green }
else { Write-Warning "No signing key at $signKey - shipping an unsigned build (no tamper verification). Put tilted-sign.key there to sign releases." }
try {
  if (Test-DevKitDll $installedDll) { throw 'Refusing to pack: Launcher\SyndicateLauncher.dll is a Dev Kit build.' }
  $code = Invoke-Dotnet8 $packer $packArgs
} finally {
  # Put the Dev Kit launcher back whatever happened.
  if (Test-Path $devKitDll) { Copy-Item -LiteralPath $devKitDll -Destination $installedDll -Force; Write-Host 'Dev Kit launcher restored in Launcher\' }
}
if ($code) { throw "Packing failed - see the message above. Nothing was handed out. Work folder: $work" }

Step 'Compiling the single encrypted EXE'
$code = Invoke-Dotnet8 $bundle @('publish', 'Bundle.csproj', '-c', 'Release', '-r', 'win-x64', '-p:UseSharedCompilation=false', '-o', (Join-Path $work 'out'))
if ($code) { throw "The EXE build failed - see the errors above. Work folder: $work" }
$built = Join-Path $work 'out\Project-Syndicate-Standalone.exe'
if (!(Test-Path $built)) { throw "Build reported success but the EXE was not found at $built." }

Step 'Delivering to the Desktop'
New-Item -ItemType Directory -Path $deliver -Force | Out-Null
$exeName = if ($Tester) { "Project-Syndicate-Tester-v$Version-b$testerBuild.exe" } elseif ($Private) { "Project-Syndicate-v$Version-private.exe" } else { "Project-Syndicate-v$Version.exe" }
$exe = Join-Path $deliver $exeName
Copy-Item $built $exe -Force
$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$exe.sha256" -Value "$hash  $exeName" -Encoding ASCII
Copy-Item (Join-Path $bundle 'component-manifest.json') (Join-Path $deliver 'component-manifest.json') -Force
Copy-Item (Join-Path $fixes 'RELEASE-READ-ME.txt') (Join-Path $deliver 'READ ME.txt') -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue

$mb = [math]::Round((Get-Item $exe).Length / 1MB, 1)
Write-Host ""
Write-Host $(if ($Private) { "Done. Private test release created (Check for updates is off in it):" } else { "Done. Public release created:" }) -ForegroundColor Green
Write-Host "  $exe  ($mb MB)"
Write-Host "  SHA256 $hash"
Write-Host "Hand out that one EXE. It contains no accounts, .env, tokens or source."
Set-Content -LiteralPath $lastFile -Value $Version -Encoding ASCII

# Announce the release in Discord via a webhook. The webhook URL is a SECRET and lives only on this PC
# (%LOCALAPPDATA%\ProjectTilted\dev\discord-webhook.txt) - never committed. The message text is public-release\
# discord-release.md ({VERSION} and {DOWNLOAD} are filled in). Any failure here never fails the release.
function Post-Discord($ver, $repo) {
  try {
    $hook = Join-Path $env:LOCALAPPDATA 'SyndicateClassic\dev\discord-webhook.txt'
    if (!(Test-Path -LiteralPath $hook)) { Write-Host "Discord: no webhook set (SyndicateClassic\dev\discord-webhook.txt) - skipped." -ForegroundColor DarkGray; return }
    $url = (Get-Content -LiteralPath $hook -Raw).Trim()
    if ($url -notmatch '^https://(discord\.com|discordapp\.com)/api/webhooks/') { Write-Host "Discord: that file is not a Discord webhook URL - skipped." -ForegroundColor Yellow; return }
    $notesFile = Join-Path $PSScriptRoot 'discord-release.md'
    if (!(Test-Path -LiteralPath $notesFile)) { Write-Host "Discord: discord-release.md missing - skipped." -ForegroundColor Yellow; return }
    $download = "https://github.com/$repo/releases/tag/v$ver"
    $text = (Get-Content -LiteralPath $notesFile -Raw).Replace('{VERSION}', $ver).Replace('{DOWNLOAD}', $download)
    if ($text.Length -gt 1900) { $text = $text.Substring(0, 1900) + "`n..." }
    $payload = @{ content = $text; allowed_mentions = @{ parse = @('roles') } } | ConvertTo-Json -Depth 5
    Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($payload)) | Out-Null
    Write-Host "Discord: posted the v$ver announcement." -ForegroundColor Green
  } catch { Write-Host "Discord: post failed ($($_.Exception.Message)) - the GitHub release still went out." -ForegroundColor Yellow }
}

# Tester builds: a GitHub PRE-release (public updaters only follow full releases), then tell the relay so every
# tester's launcher updates to it on its next start. The relay key is a SECRET that lives only on this PC:
# %LOCALAPPDATA%\ProjectTilted\dev\tester-publish-key.txt (the same word as TESTER_PUBLISH_KEY on the Worker).
if ($Tester) {
  Write-Host ""
  Write-Host "== Publishing tester build b$testerBuild" -ForegroundColor Cyan
  $ErrorActionPreference = 'Continue'
  $keyFile = Join-Path $env:LOCALAPPDATA 'SyndicateClassic\dev\tester-publish-key.txt'
  if (!$testerRepo) { Write-Host "No public-release\update-repo.txt: send testers the EXE by hand." -ForegroundColor Yellow }
  elseif (!(Get-Command gh -ErrorAction SilentlyContinue)) { Write-Host "GitHub CLI (gh) not found: send testers the EXE by hand." -ForegroundColor Yellow }
  else {
    & gh release create "tester-b$testerBuild" $exe "$exe.sha256" --repo $testerRepo --prerelease --title "Tester build b$testerBuild (v$Version)" --notes "Syndicate Tester build. It only opens for members with the Tester role in the Syndicate Discord."
    if ($LASTEXITCODE) { Write-Host "Uploading the tester build failed - see above." -ForegroundColor Yellow }
    elseif (!(Test-Path -LiteralPath $keyFile)) { Write-Host "Uploaded, but testers won't auto-update: put the relay's TESTER_PUBLISH_KEY in $keyFile and run this again." -ForegroundColor Yellow }
    else {
      try {
        $body = @{ build = $testerBuild; version = $Version; url = "https://github.com/$testerRepo/releases/download/tester-b$testerBuild/$exeName"; sha256 = $hash } | ConvertTo-Json
        Invoke-RestMethod -Uri ($Relay.TrimEnd('/') + '/tester/publish') -Method Post -ContentType 'application/json' -Headers @{ 'x-publish-key' = (Get-Content -LiteralPath $keyFile -Raw).Trim() } -Body $body | Out-Null
        Write-Host "Published. Testers get b$testerBuild the next time they open Syndicate Tester." -ForegroundColor Green
      } catch { Write-Host "The relay refused the tester build ($($_.Exception.Message)). Check the key in $keyFile." -ForegroundColor Yellow }
    }
  }
}

if ($UpdateRepo) {
  Write-Host ""
  Write-Host "== Pushing the update to players" -ForegroundColor Cyan
  $gh = Get-Command gh -ErrorAction SilentlyContinue
  $published = $false
  $ErrorActionPreference = 'Continue'  # gh writes to stderr; Windows PowerShell 5.1 would stop on it
  if ($gh) {
    & gh auth status *> $null
    if ($LASTEXITCODE -eq 0) {
      $yes = Read-Host "Publish v$Version to github.com/$UpdateRepo now so players can update? (y/n)"
      if ($yes -match '^[yY]') {
        & gh release create "v$Version" $exe "$exe.sha256" --repo $UpdateRepo --title "Syndicate v$Version" --notes "Press Check for updates in Syndicate (under Settings on the left) to install this version."
        if ($LASTEXITCODE) { Write-Host "Publishing failed - see above. You can upload by hand (steps below)." -ForegroundColor Yellow } else { $published = $true; Write-Host "Published. Players get v$Version when they press Check for updates." -ForegroundColor Green }
        if ($published) { Post-Discord $Version $UpdateRepo }
      }
    }
  }
  if (!$published) {
    Write-Host "To push this update to players:"
    Write-Host "  1. Open https://github.com/$UpdateRepo/releases/new"
    Write-Host "  2. Tag: v$Version   (must be higher than the last release)"
    Write-Host "  3. Attach BOTH files: Project-Syndicate-v$Version.exe and Project-Syndicate-v$Version.exe.sha256"
    Write-Host "  4. Click Publish release. Players update the next time they open Syndicate."
  }
}
Start-Process explorer.exe $deliver
