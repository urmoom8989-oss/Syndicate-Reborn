# UPDATE FROM GIT: puts the source from this GitHub clone into your Syndicate folder and rebuilds
# what changed. Pull in GitHub Desktop first, then run "UPDATE FROM GIT.cmd".
#   * Runtime files from the repo (backend data, configs, game-10.40.json) are copied into Syndicate\Runtime
#   * Syndicate's backend (SyndicateClassic\backend) is copied to Syndicate\Syndicate-backend; the first time,
#     node_modules and Dependencies\Node + MongoDB are copied from an older install
#   * the game server (Syndicate1040.dll + SyndicateConsoleRecovery.dll) is rebuilt when ServerAndConsole
#     changed (needs Visual Studio with "Desktop development with C++")
#   * the Syndicate menu (SyndicateMenu.dll) is rebuilt when SyndicateMenu\ changed (same Visual Studio)
#   * Erbium.dll and SyndicateStudio.dll (Syndicate Studio's in-game helper) are rebuilt from SyndicateClassic\game-server
#   * Runtime\dlls\manifest.json is refreshed so the launcher accepts the new DLLs
#   * the launcher (SyndicateLauncher.exe) is rebuilt when its source or the runtime changed (needs the .NET 8 SDK)
# Your launcher profiles, lockers and settings (in %APPDATA%\SyndicateLauncher) are never touched.
# -Force rebuilds everything even if nothing changed. -Root picks the Syndicate folder.
param([switch]$Force, [string]$Root = "")
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$repo = Split-Path $PSScriptRoot -Parent

Stop-IfRunning
$root = Find-SyndicateRoot $Root
Write-Host "Syndicate folder:       $root"
Write-Host "Source (GitHub clone): $repo"
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $root "_backups\git-update-$stamp"
$summary = New-Object System.Collections.Generic.List[string]
$runtimeChanged = $false

function Backup-File([string]$path, [string]$relative) {
  if (!(Test-Path -LiteralPath $path)) { return }
  $b = Join-Path $backup $relative
  New-Item -ItemType Directory -Path (Split-Path $b) -Force | Out-Null
  Copy-Item -LiteralPath $path -Destination $b -Force
}

# ---------------------------------------------------------------- old names
# Folders and files made before the rename to Syndicate are moved to their new names once.
$oldPrefix = 'Tit' + 'anium'
$legacyBackend = Join-Path $root ('Til' + 'ted-backend')
if ((Test-Path -LiteralPath $legacyBackend) -and !(Test-Path -LiteralPath (Join-Path $root 'Syndicate-backend'))) {
  Rename-Item -LiteralPath $legacyBackend -NewName 'Syndicate-backend'
  Write-Host '  renamed the old backend folder to Syndicate-backend'
}
foreach ($pair in @(@("$($oldPrefix)1040.dll", 'Syndicate1040.dll'), @("$($oldPrefix)ConsoleRecovery.dll", 'SyndicateConsoleRecovery.dll'))) {
  $oldDll = Join-Path $root ('Runtime\dlls\' + $pair[0])
  $newDll = Join-Path $root ('Runtime\dlls\' + $pair[1])
  if (!(Test-Path -LiteralPath $oldDll)) { continue }
  if (Test-Path -LiteralPath $newDll) { Remove-Item -LiteralPath $oldDll -Force }
  else { Rename-Item -LiteralPath $oldDll -NewName $pair[1] }
  Write-Host "  Runtime\dlls\$($pair[0]) is now $($pair[1])"
}

# ---------------------------------------------------------------- runtime files
Step 'Runtime files (backend data, configs)'
$runtimeSrc = Join-Path $repo 'Launcher\SyndicateLauncher\Runtime'
$changed = 0
foreach ($file in (Get-ChildItem -LiteralPath $runtimeSrc -Recurse -File)) {
  $relative = $file.FullName.Substring($runtimeSrc.Length + 1)
  # manifest.json is rebuilt from the DLLs actually in your folder (below); binaries come from the dev kit or the build.
  if ($relative -ieq 'dlls\manifest.json' -or $file.Extension -in @('.exe', '.dll')) { continue }
  $dst = Join-Path $root ('Runtime\' + $relative)
  if ((Test-Path -LiteralPath $dst) -and (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) { continue }
  Backup-File $dst ('Runtime\' + $relative)
  New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
  Copy-Item -LiteralPath $file.FullName -Destination $dst -Force
  Write-Host "  updated Runtime\$relative"
  $changed++
}
if ($changed -eq 0) { Write-Host '  already up to date'; $summary.Add('Runtime files: already up to date') }
else { $runtimeChanged = $true; $summary.Add("Runtime files: $changed updated (old copies in _backups\git-update-$stamp)") }

# ---------------------------------------------------------------- Syndicate backend
# The launcher starts Syndicate's backend (Node + MongoDB) from Syndicate\Syndicate-backend, with
# Node and MongoDB from Syndicate\Dependencies (SyndicateBackend.cs). Source comes from the repo;
# node_modules and the runtimes come once from an older install.
Step 'Backend (Syndicate\Syndicate-backend)'
$backendSrc = Join-Path $repo 'SyndicateClassic\backend'
$backendDst = Join-Path $root 'Syndicate-backend'
$deps = Join-Path $root 'Dependencies'
$donor = $null
if (!(Test-Path (Join-Path $backendDst 'node_modules')) -or !(Test-Path (Join-Path $deps 'Node\node.exe')) -or !(Test-Path (Join-Path $deps 'MongoDB\mongod.exe'))) {
  $donor = Find-SyndicateInstall
  if ($donor) { Write-Host "  using the older install for node_modules / Node / MongoDB: $($donor.Root)" }
}
if (!(Test-Path $backendSrc)) {
  Write-Host '  skipped: SyndicateClassic\backend is not in this clone' -ForegroundColor Yellow
  $summary.Add('Backend: SKIPPED - no SyndicateClassic\backend in the clone')
} else {
  # Source files: everything except installed packages, secrets and runtime data.
  & robocopy $backendSrc $backendDst /E /XD node_modules .git logs /XF .env jwt.secret tokens.json *.log /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw 'Copying the backend source failed.' }
  if ($LASTEXITCODE -band 1) { Write-Host '  backend source updated' } else { Write-Host '  backend source already up to date' }
  # A fresh install has no .env. The example uses localhost ports and no secrets.
  $backendEnv = Join-Path $backendDst '.env'
  $backendExample = Join-Path $backendSrc '.env.example'
  if (!(Test-Path -LiteralPath $backendEnv) -and (Test-Path -LiteralPath $backendExample)) {
    Copy-Item -LiteralPath $backendExample -Destination $backendEnv -Force
    Write-Host '  created Syndicate-backend\.env from .env.example (localhost defaults)'
  }
  # node_modules
  if (!(Test-Path (Join-Path $backendDst 'node_modules'))) {
    if ($donor) {
      Write-Host '  copying node_modules (first time, can take a minute) ...'
      & robocopy (Join-Path $donor.Backend 'node_modules') (Join-Path $backendDst 'node_modules') /E /NFL /NDL /NJH /NJS /NP | Out-Null
      if ($LASTEXITCODE -ge 8) { throw 'Copying node_modules failed.' }
    } else {
      Write-Host '  node_modules missing and no older install found to copy it from.' -ForegroundColor Yellow
      Write-Host '  Get the dev kit (it includes Syndicate-backend), or put an older install in your Documents folder, then run this again.' -ForegroundColor Yellow
    }
  }
  # Node + MongoDB runtimes
  foreach ($pair in @(@('Node', 'node.exe'), @('MongoDB', 'mongod.exe'))) {
    $want = Join-Path $deps ($pair[0] + '\' + $pair[1])
    if (Test-Path $want) { continue }
    $from = if ($donor) { Join-Path $donor.Root ('Dependencies\' + $pair[0]) } else { $null }
    if ($from -and (Test-Path (Join-Path $from $pair[1]))) {
      Write-Host "  copying Dependencies\$($pair[0]) (first time) ..."
      & robocopy $from (Join-Path $deps $pair[0]) /E /NFL /NDL /NJH /NJS /NP | Out-Null
      if ($LASTEXITCODE -ge 8) { throw "Copying Dependencies\$($pair[0]) failed." }
    } else {
      Write-Host "  missing Dependencies\$($pair[0])\$($pair[1]) and no older install found to copy it from." -ForegroundColor Yellow
    }
  }
  # Top up packages when package.json / package-lock.json changed (or after copying an older
  # node_modules from a Syndicate install). Needs internet; uses the bundled Node's npm if present.
  $pkgHash = Get-TreeHash @((Join-Path $backendSrc 'package.json'), (Join-Path $backendSrc 'package-lock.json'))
  if ((Test-Path (Join-Path $backendDst 'node_modules')) -and ($Force -or $pkgHash -ne (Get-DevState 'backend-packages'))) {
    $npm = Join-Path $deps 'Node\npm.cmd'
    if (!(Test-Path $npm)) { $cmd = Get-Command npm.cmd -ErrorAction SilentlyContinue; $npm = if ($cmd) { $cmd.Source } else { $null } }
    if ($npm) {
      $nlog = Join-Path $DevState 'npm-install-log.txt'
      Write-Host "  installing backend packages (npm install) ... log: $nlog"
      $nodeDir = Join-Path $deps 'Node'
      $savedPath = $env:Path
      if (Test-Path (Join-Path $nodeDir 'node.exe')) { $env:Path = $nodeDir + ';' + $env:Path }
      Push-Location $backendDst
      try { $code = Invoke-Native { & $npm install --omit=dev --no-audit --no-fund *> $nlog } } finally { Pop-Location; $env:Path = $savedPath }
      if ($code -eq 0) { Set-DevState 'backend-packages' $pkgHash; Write-Host '  backend packages up to date' }
      else { Write-Host "  npm install failed (exit $code); the copied node_modules are kept. Log: $nlog" -ForegroundColor Yellow }
    } else {
      Write-Host '  npm not found; skipping the package check (the copied node_modules are used as they are).' -ForegroundColor Yellow
    }
  }
  $ok = (Test-Path (Join-Path $backendDst 'node_modules')) -and (Test-Path (Join-Path $deps 'Node\node.exe')) -and (Test-Path (Join-Path $deps 'MongoDB\mongod.exe'))
  if ($ok) { $summary.Add('Backend: ready (Syndicate-backend + Node + MongoDB)') }
  else { $summary.Add('Backend: INCOMPLETE - see the yellow lines above; the launcher cannot start the game yet') }
}

# ---------------------------------------------------------------- game server
Step 'Game server (Syndicate1040.dll, SyndicateConsoleRecovery.dll)'
$serverSrc = Join-Path $repo 'ServerAndConsole'
$project = Join-Path $serverSrc '10.40.vcxproj'
$serverHash = Get-TreeHash @($serverSrc)
$dlls = Join-Path $root 'Runtime\dlls'
$builtDlls = @('Syndicate1040.dll', 'SyndicateConsoleRecovery.dll')
$haveDlls = @($builtDlls | Where-Object { Test-Path (Join-Path $dlls $_) }).Count -eq $builtDlls.Count
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = $null
if (Test-Path $vswhere) {
  $vs = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | Out-String) | ConvertFrom-Json | Select-Object -First 1
}
if (!$Force -and $haveDlls -and $serverHash -eq (Get-DevState 'gameserver')) {
  Write-Host '  no game server changes since the last build'
  $summary.Add('Game server: no changes')
} elseif (!$vs) {
  Write-Host '  skipped: install Visual Studio with "Desktop development with C++" to build the game server.' -ForegroundColor Yellow
  Write-Host '  Until then you keep the DLLs you already have (from the dev kit).' -ForegroundColor Yellow
  $summary.Add('Game server: SKIPPED - Visual Studio (C++) not installed; using the current DLLs')
} else {
  $msbuild = Join-Path $vs.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
  if (!(Test-Path $msbuild)) { throw "MSBuild.exe not found in $($vs.installationPath). Repair Visual Studio with the C++ workload." }
  # The project targets v145 (Visual Studio 2026). Visual Studio 2022 has v143, which builds it the same way.
  $major = [int]($vs.installationVersion.Split('.')[0])
  $toolset = if ($major -ge 18) { 'v145' } else { 'v143' }
  $build = Join-Path $DevState 'gameserver-build'
  # Trailing double backslash: survives Windows quoting when the path has spaces (a single \" would escape the quote).
  $out = (Join-Path $build 'out') + '\\'
  if (Test-Path $out) { Remove-Item -LiteralPath $out -Recurse -Force }
  $log = Join-Path $build 'last-build-log.txt'
  New-Item -ItemType Directory -Path $build -Force | Out-Null
  if (Test-Path $log) { Remove-Item -LiteralPath $log -Force }
  Write-Host "  Visual Studio $($vs.installationVersion), toolset $toolset"
  Write-Host "  build log: $log"
  foreach ($target in @(@{ Name = 'Syndicate1040.dll'; Console = 'false'; Int = 'int-server' }, @{ Name = 'SyndicateConsoleRecovery.dll'; Console = 'true'; Int = 'int-console' })) {
    Write-Host "  building $($target.Name) ..."
    $msArgs = @($project, '/t:Build', '/p:Configuration=Release', '/p:Platform=x64', "/p:PlatformToolset=$toolset",
      "/p:SyndicateClientConsole=$($target.Console)", "/p:OutDir=$out", "/p:IntDir=$(Join-Path $build $target.Int)\\", '/m', '/nologo', '/v:minimal')
    $code = Invoke-Native { & $msbuild @msArgs *>> $log }
    if ($code -ne 0 -or !(Test-Path (Join-Path $out $target.Name))) {
      Get-Content -LiteralPath $log | Select-String -Pattern ': error ' | Select-Object -First 15 | ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red }
      throw "Building $($target.Name) failed. Full log: $log"
    }
  }
  foreach ($name in $builtDlls) {
    $dst = Join-Path $dlls $name
    Backup-File $dst ("Runtime\dlls\$name")
    Copy-Item -LiteralPath (Join-Path $out $name) -Destination $dst -Force
    Write-Host "  installed Runtime\dlls\$name"
  }
  Set-DevState 'gameserver' $serverHash
  $runtimeChanged = $true
  $summary.Add('Game server: rebuilt')
}

# ---------------------------------------------------------------- Syndicate menu
# SyndicateMenu.dll (the in-game Syndicate menu, Insert to open) is built from SyndicateMenu\ when
# that source changes. The launcher loads it in every season when "Syndicate menu" is on.
Step 'Syndicate menu (SyndicateMenu.dll)'
$menuSrc = Join-Path $repo 'SyndicateMenu\SyndicateMenu'
$menuProject = Join-Path $menuSrc 'SyndicateMenu.vcxproj'
$menuDst = Join-Path $dlls 'SyndicateMenu.dll'
if (!(Test-Path $menuProject)) {
  Write-Host '  skipped: SyndicateMenu\SyndicateMenu\SyndicateMenu.vcxproj is not in this clone' -ForegroundColor Yellow
  $summary.Add('Syndicate menu: SKIPPED - source not in the clone')
} else {
  $menuHash = Get-TreeHash @($menuSrc)
  if (!$Force -and (Test-Path $menuDst) -and $menuHash -eq (Get-DevState 'menu')) {
    Write-Host '  no menu changes since the last build'
    $summary.Add('Syndicate menu: no changes')
  } elseif (!$vs) {
    Write-Host '  skipped: install Visual Studio with "Desktop development with C++" to build the menu.' -ForegroundColor Yellow
    $summary.Add('Syndicate menu: SKIPPED - Visual Studio (C++) not installed; using the current DLL')
  } else {
    $msbuild = Join-Path $vs.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
    if (!(Test-Path $msbuild)) { throw "MSBuild.exe not found in $($vs.installationPath). Repair Visual Studio with the C++ workload." }
    $major = [int]($vs.installationVersion.Split('.')[0])
    $toolset = if ($major -ge 18) { 'v145' } else { 'v143' }
    $mbuild = Join-Path $DevState 'menu-build'
    $mout = (Join-Path $mbuild 'out') + '\\'
    if (Test-Path $mout) { Remove-Item -LiteralPath $mout -Recurse -Force }
    $mlog = Join-Path $mbuild 'last-build-log.txt'
    New-Item -ItemType Directory -Path $mbuild -Force | Out-Null
    if (Test-Path $mlog) { Remove-Item -LiteralPath $mlog -Force }
    Write-Host '  building SyndicateMenu.dll ...'
    Write-Host "  build log: $mlog"
    $msArgs = @($menuProject, '/t:Build', '/p:Configuration=Release', '/p:Platform=x64', "/p:PlatformToolset=$toolset",
      "/p:OutDir=$mout", "/p:IntDir=$(Join-Path $mbuild 'int')\\", '/m', '/nologo', '/v:minimal')
    $code = Invoke-Native { & $msbuild @msArgs *>> $mlog }
    if ($code -ne 0 -or !(Test-Path (Join-Path $mout 'SyndicateMenu.dll'))) {
      Get-Content -LiteralPath $mlog | Select-String -Pattern ': error ' | Select-Object -First 15 | ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red }
      throw "Building SyndicateMenu.dll failed. Full log: $mlog"
    }
    Backup-File $menuDst 'Runtime\dlls\SyndicateMenu.dll'
    Copy-Item -LiteralPath (Join-Path $mout 'SyndicateMenu.dll') -Destination $menuDst -Force
    Write-Host '  installed Runtime\dlls\SyndicateMenu.dll'
    Set-DevState 'menu' $menuHash
    $runtimeChanged = $true
    $summary.Add('Syndicate menu: rebuilt')
  }
}

# ---------------------------------------------------------------- Syndicate components
# The Chapter 1 (Syndicate) builds need Erbium.dll and cobalt.dll. A dev kit can carry them
# in Runtime\dlls so no older install or fixed PC path is required. If an older Project
# Tilted install is present, copy the current components from it; otherwise keep what is there.
Step 'Syndicate components (Erbium, cobalt, client mods)'
$syndicateRoots = @()
if ($env:TILTED_ROOT) { $syndicateRoots += $env:TILTED_ROOT }
$documents = [Environment]::GetFolderPath('MyDocuments')
$syndicateRoots += @(
  (Join-Path $root 'Syndicate'),
  (Join-Path $root ('Til' + 'ted')),
  (Join-Path $root 'Project Tilted'),
  (Join-Path $root 'Runtime\Syndicate'),
  (Join-Path $root ('Runtime\Til' + 'ted')),
  (Join-Path $documents 'Project Tilted'),
  (Join-Path $documents 'tiltedlegacy'),
  (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Project Tilted'),
  (Join-Path ([Environment]::GetFolderPath('Desktop')) 'tiltedlegacy'),
  (Join-Path $repo 'SyndicateClassic'),
  (Join-Path $repo 'Launcher\SyndicateLauncher\Runtime\dlls'),
  $dlls
)
# An older install found anywhere Find-SyndicateInstall looks (Documents, Desktop, Downloads, one folder deeper).
if (!$donor) { $donor = Find-SyndicateInstall }
if ($donor) { $syndicateRoots += $donor.Root }
$syndicateRoots = @($syndicateRoots | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -Unique)
$syndicateComponents = @(
  @{ Name = 'Erbium.dll'; Sources = @('Erbium-Quality-of-life-changes-main\Erbium\x64\Release\Erbium.dll', 'Source\Erbium\Erbium\x64\Release\Erbium.dll', 'Launcher\components\Erbium-Quality-of-life-changes-main\Erbium\x64\Release\Erbium.dll', 'Erbium.dll') },
  @{ Name = 'cobalt.dll'; Sources = @('Launcher\cobalt.dll', 'Launcher\components\cobalt.dll', 'cobalt.dll') },
  # The prebuilt client modules keep their old file names in older installs; here they are installed as Syndicate*.dll.
  @{ Name = 'SyndicateClient.dll'; Sources = @('Launcher\TiltedClient.dll', 'Launcher\components\TiltedClient.dll', 'TiltedClient.dll') },
  @{ Name = 'SyndicatePadAssist.dll'; Sources = @('Launcher\TiltedPadAssist.dll', 'Launcher\components\TiltedPadAssist.dll', 'TiltedPadAssist.dll') }
)
$syndicateCopied = 0
New-Item -ItemType Directory -Path $dlls -Force | Out-Null
foreach ($component in $syndicateComponents) {
  $dst = Join-Path $dlls $component.Name
  $src = $null
  foreach ($syndicateRoot in $syndicateRoots) {
    foreach ($relativePath in $component.Sources) {
      $candidate = Join-Path $syndicateRoot $relativePath
      if ((Test-Path -LiteralPath $candidate) -and ($candidate -ne $dst)) { $src = $candidate; break }
    }
    if ($src) { break }
  }
  if (!$src) {
    if (Test-Path -LiteralPath $dst) { Write-Host "  $($component.Name): keeping existing copy" }
    else { Write-Host "  $($component.Name): not found - put it in Runtime\dlls (the dev kit has it)" -ForegroundColor Yellow }
    continue
  }
  Backup-File $dst ("Runtime\dlls\$($component.Name)")
  Copy-Item -LiteralPath $src -Destination $dst -Force
  Write-Host "  installed Runtime\dlls\$($component.Name)"
  $syndicateCopied++
  $runtimeChanged = $true
}
if ($syndicateCopied -eq 0) { $summary.Add('Syndicate components: already present or unavailable') }
else { $summary.Add("Syndicate components: $syndicateCopied installed from the older install") }
# Files from before the Syndicate names (now SyndicateMenu.dll, SyndicateClient.dll, SyndicatePadAssist.dll).
foreach ($old in @(('Tit' + 'aniumMenu.dll'), ('Til' + 'tedClient.dll'), ('Til' + 'tedPadAssist.dll'))) {
  $oldPath = Join-Path $dlls $old
  if (Test-Path -LiteralPath $oldPath) {
    try { Backup-File $oldPath "Runtime\dlls\$old" } catch { }
    Remove-Item -LiteralPath $oldPath -Force -ErrorAction SilentlyContinue
    Write-Host "  removed the old Runtime\dlls\$old (renamed to Syndicate)"
    $runtimeChanged = $true
  }
}

# ---------------------------------------------------------------- Erbium from source
# Builds a fresh Erbium.dll from SyndicateClassic\game-server (the repo's Erbium source) and puts it
# in Runtime\dlls, replacing any older copy taken from a Syndicate install above. The build is
# cached in %LOCALAPPDATA%\PassionProject\dev\erbium-build and only redone when the source changes.
Step 'Erbium (fresh build from SyndicateClassic\game-server)'
$erbiumSrc = Join-Path $repo 'SyndicateClassic\game-server'
$erbiumProject = Join-Path $erbiumSrc 'Erbium\Erbium.vcxproj'
$ebuild = Join-Path $DevState 'erbium-build'
$ebuilt = Join-Path $ebuild 'out\Erbium.dll'
if (!(Test-Path $erbiumProject)) {
  Write-Host '  skipped: SyndicateClassic\game-server is not in this clone' -ForegroundColor Yellow
  $summary.Add('Erbium: SKIPPED - no source in the clone')
} elseif (!$vs) {
  Write-Host '  skipped: install Visual Studio with "Desktop development with C++" to build Erbium.' -ForegroundColor Yellow
  $summary.Add('Erbium: SKIPPED - Visual Studio (C++) not installed; using the copied Erbium.dll')
} else {
  $erbiumHash = Get-TreeHash @((Join-Path $erbiumSrc 'Erbium'), (Join-Path $erbiumSrc 'SDK'))
  if ($Force -or !(Test-Path $ebuilt) -or $erbiumHash -ne (Get-DevState 'erbium')) {
    $emsbuild = Join-Path $vs.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
    if (!(Test-Path $emsbuild)) { throw "MSBuild.exe not found in $($vs.installationPath). Repair Visual Studio with the C++ workload." }
    # Erbium targets v143 (Visual Studio 2022); Visual Studio 2026 builds it with v145.
    $etoolset = if ([int]($vs.installationVersion.Split('.')[0]) -ge 18) { 'v145' } else { 'v143' }
    $eout = (Join-Path $ebuild 'out') + '\\'
    if (Test-Path $eout) { Remove-Item -LiteralPath $eout -Recurse -Force }
    New-Item -ItemType Directory -Path $ebuild -Force | Out-Null
    $elog = Join-Path $ebuild 'last-build-log.txt'
    if (Test-Path $elog) { Remove-Item -LiteralPath $elog -Force }
    Write-Host "  building Erbium.dll (Visual Studio $($vs.installationVersion), toolset $etoolset; a few minutes) ..."
    Write-Host "  build log: $elog"
    $msArgs = @($erbiumProject, '/t:Build', '/p:Configuration=Release', '/p:Platform=x64', "/p:PlatformToolset=$etoolset",
      "/p:SolutionDir=$erbiumSrc\\", "/p:OutDir=$eout", "/p:IntDir=$(Join-Path $ebuild 'int')\\", '/m', '/nologo', '/v:minimal')
    $code = Invoke-Native { & $emsbuild @msArgs *>> $elog }
    if ($code -ne 0 -or !(Test-Path $ebuilt)) {
      Get-Content -LiteralPath $elog | Select-String -Pattern ': error ' | Select-Object -First 15 | ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red }
      throw "Building Erbium.dll failed. Full log: $elog"
    }
    Set-DevState 'erbium' $erbiumHash
    $summary.Add('Erbium: rebuilt from source')
  } else {
    Write-Host '  no Erbium source changes since the last build'
    $summary.Add('Erbium: no changes (using the cached build)')
  }
  # Install the fresh build (the step above may have replaced it with an older copy).
  # Windows Security often flags a self-built Erbium.dll by mistake. Then the fresh build cannot be read:
  # keep the Erbium.dll already in Runtime\dlls, say how to allow it, and carry on with the update.
  $edst = Join-Path $dlls 'Erbium.dll'
  $builtHash = if (Test-Path -LiteralPath $ebuilt) { Get-SafeHash $ebuilt } else { $null }
  if (!$builtHash) {
    Write-BlockedHelp $ebuilt
    if (Test-Path -LiteralPath $edst) { Write-Host '  kept the existing Runtime\dlls\Erbium.dll for now' -ForegroundColor Yellow }
    $summary.Add('Erbium: fresh build BLOCKED by Windows Security - kept the existing Erbium.dll (see the steps above)')
  } elseif (!(Test-Path -LiteralPath $edst) -or (Get-SafeHash $edst) -ne $builtHash) {
    try { Backup-File $edst 'Runtime\dlls\Erbium.dll' } catch { Write-Host '  (could not back up the old Erbium.dll)' -ForegroundColor Yellow }
    Copy-Item -LiteralPath $ebuilt -Destination $edst -Force
    Write-Host '  installed the fresh Runtime\dlls\Erbium.dll'
    $runtimeChanged = $true
  }
}

# ---------------------------------------------------------------- Syndicate Studio
# SyndicateStudio.dll: loaded into your game by Syndicate Studio (the Studio button under Play in the dev launcher)
# for the Outliner, gizmo, free cam and placing. Built from SyndicateClassic\game-server\SyndicateStudio (it shares
# Erbium's SDK), cached in %LOCALAPPDATA%\PassionProject\dev\studio-build. Optional: without it the Studio still runs
# from the match server's files, so a failed build only warns.
Step 'Syndicate Studio (SyndicateStudio.dll)'
$studioSrc = Join-Path $repo 'SyndicateClassic\game-server\SyndicateStudio'
$studioProject = Join-Path $studioSrc 'SyndicateStudio.vcxproj'
$sbuild = Join-Path $DevState 'studio-build'
$sbuilt = Join-Path $sbuild 'out\SyndicateStudio.dll'
$sdst = Join-Path $dlls 'SyndicateStudio.dll'
if (!(Test-Path $studioProject)) {
  Write-Host '  skipped: SyndicateClassic\game-server\SyndicateStudio is not in this clone' -ForegroundColor Yellow
  $summary.Add('Syndicate Studio: SKIPPED - no source in the clone')
} elseif (!$vs) {
  Write-Host '  skipped: install Visual Studio with "Desktop development with C++" to build SyndicateStudio.dll.' -ForegroundColor Yellow
  $summary.Add('Syndicate Studio: SKIPPED - Visual Studio (C++) not installed')
} else {
  $studioHash = Get-TreeHash @($studioSrc, (Join-Path $repo 'SyndicateClassic\game-server\SDK'))
  $studioOk = $true
  if ($Force -or !(Test-Path $sbuilt) -or $studioHash -ne (Get-DevState 'studio')) {
    $smsbuild = Join-Path $vs.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
    $stoolset = if ([int]($vs.installationVersion.Split('.')[0]) -ge 18) { 'v145' } else { 'v143' }
    $sout = (Join-Path $sbuild 'out') + '\\'
    if (Test-Path $sout) { Remove-Item -LiteralPath $sout -Recurse -Force }
    New-Item -ItemType Directory -Path $sbuild -Force | Out-Null
    $slog = Join-Path $sbuild 'last-build-log.txt'
    if (Test-Path $slog) { Remove-Item -LiteralPath $slog -Force }
    Write-Host "  building SyndicateStudio.dll (toolset $stoolset) ..."
    Write-Host "  build log: $slog"
    $msArgs = @($studioProject, '/t:Build', '/p:Configuration=Release', '/p:Platform=x64', "/p:PlatformToolset=$stoolset",
      "/p:OutDir=$sout", "/p:IntDir=$(Join-Path $sbuild 'int')\\", '/m', '/nologo', '/v:minimal')
    $code = if (Test-Path $smsbuild) { Invoke-Native { & $smsbuild @msArgs *>> $slog } } else { 1 }
    if (($code -ne 0 -or !(Test-Path $sbuilt)) -and (Test-Path $smsbuild)) {
      # An incremental build can trip over its own cached files (C1382: the precompiled header changed since an
      # object was built). Try once more from scratch before calling it failed.
      Write-Host '  retrying with a clean rebuild ...'
      $msArgs[1] = '/t:Rebuild'
      $code = Invoke-Native { & $smsbuild @msArgs *>> $slog }
    }
    if ($code -ne 0 -or !(Test-Path $sbuilt)) {
      if (Test-Path $slog) { Get-Content -LiteralPath $slog | Select-String -Pattern ': error ' | Select-Object -First 15 | ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red } }
      Write-Host "  Building SyndicateStudio.dll failed (full log: $slog). The Studio still opens, without its in-game helper." -ForegroundColor Yellow
      $summary.Add('Syndicate Studio: BUILD FAILED - see ' + $slog)
      $studioOk = $false
    } else {
      Set-DevState 'studio' $studioHash
      $summary.Add('Syndicate Studio: rebuilt')
    }
  } else {
    Write-Host '  no Studio source changes since the last build'
    $summary.Add('Syndicate Studio: no changes (using the cached build)')
  }
  if ($studioOk) {
    # Like Erbium, Windows Security may flag a self-built DLL by mistake: keep going and say how to allow it.
    $sbuiltHash = if (Test-Path -LiteralPath $sbuilt) { Get-SafeHash $sbuilt } else { $null }
    if (!$sbuiltHash) {
      Write-BlockedHelp $sbuilt
      $summary.Add('Syndicate Studio: fresh build BLOCKED by Windows Security (see the steps above)')
    } elseif (!(Test-Path -LiteralPath $sdst) -or (Get-SafeHash $sdst) -ne $sbuiltHash) {
      try { Backup-File $sdst 'Runtime\dlls\SyndicateStudio.dll' } catch { }
      Copy-Item -LiteralPath $sbuilt -Destination $sdst -Force
      Write-Host '  installed Runtime\dlls\SyndicateStudio.dll'
      $runtimeChanged = $true
    }
  }
}

# ---------------------------------------------------------------- client mods (SyndicateClient.dll)
# SyndicateClient.dll (FOV, turbo build, Save the World heroes in the locker...) is loaded into every Chapter 1 / 2
# game. It used to be only copied from an older install (above); it is now built from
# SyndicateClassic\game-server\ErbiumClient (SyndicateClient.vcxproj), cached in
# %LOCALAPPDATA%\PassionProject\dev\client-build, and replaces that copy. Optional: when the build fails, the copy
# from the older install stays, so nothing stops working.
Step 'Client mods (SyndicateClient.dll)'
$clientSrc = Join-Path $repo 'SyndicateClassic\game-server\ErbiumClient'
$clientProject = Join-Path $clientSrc 'SyndicateClient.vcxproj'
$cbuild = Join-Path $DevState 'client-build'
$cbuilt = Join-Path $cbuild 'out\SyndicateClient.dll'
$cdst = Join-Path $dlls 'SyndicateClient.dll'
if (!(Test-Path $clientProject)) {
  Write-Host '  skipped: SyndicateClassic\game-server\ErbiumClient is not in this clone' -ForegroundColor Yellow
  $summary.Add('Client mods: SKIPPED - no source in the clone')
} elseif (!$vs) {
  Write-Host '  skipped: install Visual Studio with "Desktop development with C++" to build SyndicateClient.dll.' -ForegroundColor Yellow
  $summary.Add('Client mods: SKIPPED - Visual Studio (C++) not installed (keeping the existing copy)')
} else {
  $clientHash = Get-TreeHash @($clientSrc, (Join-Path $repo 'SyndicateClassic\game-server\SDK'), (Join-Path $repo 'SyndicateClassic\game-server\Erbium\Erbium\Public'))
  $clientOk = $true
  if ($Force -or !(Test-Path $cbuilt) -or $clientHash -ne (Get-DevState 'client')) {
    $cmsbuild = Join-Path $vs.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
    $ctoolset = if ([int]($vs.installationVersion.Split('.')[0]) -ge 18) { 'v145' } else { 'v143' }
    $cout = (Join-Path $cbuild 'out') + '\\'
    if (Test-Path $cout) { Remove-Item -LiteralPath $cout -Recurse -Force }
    New-Item -ItemType Directory -Path $cbuild -Force | Out-Null
    $clog = Join-Path $cbuild 'last-build-log.txt'
    if (Test-Path $clog) { Remove-Item -LiteralPath $clog -Force }
    Write-Host "  building SyndicateClient.dll (toolset $ctoolset) ..."
    Write-Host "  build log: $clog"
    $msArgs = @($clientProject, '/t:Build', '/p:Configuration=Release', '/p:Platform=x64', "/p:PlatformToolset=$ctoolset",
      "/p:OutDir=$cout", "/p:IntDir=$(Join-Path $cbuild 'int')\\", '/m', '/nologo', '/v:minimal')
    $code = if (Test-Path $cmsbuild) { Invoke-Native { & $cmsbuild @msArgs *>> $clog } } else { 1 }
    if (($code -ne 0 -or !(Test-Path $cbuilt)) -and (Test-Path $cmsbuild)) {
      Write-Host '  retrying with a clean rebuild ...'
      $msArgs[1] = '/t:Rebuild'
      $code = Invoke-Native { & $cmsbuild @msArgs *>> $clog }
    }
    if ($code -ne 0 -or !(Test-Path $cbuilt)) {
      if (Test-Path $clog) { Get-Content -LiteralPath $clog | Select-String -Pattern ': error ' | Select-Object -First 15 | ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red } }
      Write-Host "  Building SyndicateClient.dll failed (full log: $clog). Keeping the existing copy." -ForegroundColor Yellow
      $summary.Add('Client mods: BUILD FAILED - see ' + $clog + ' (keeping the existing copy)')
      $clientOk = $false
    } else {
      Set-DevState 'client' $clientHash
      $summary.Add('Client mods: rebuilt')
    }
  } else {
    Write-Host '  no client source changes since the last build'
    $summary.Add('Client mods: no changes (using the cached build)')
  }
  if ($clientOk) {
    $cbuiltHash = if (Test-Path -LiteralPath $cbuilt) { Get-SafeHash $cbuilt } else { $null }
    if (!$cbuiltHash) {
      Write-BlockedHelp $cbuilt
      $summary.Add('Client mods: fresh build BLOCKED by Windows Security (see the steps above)')
    } elseif (!(Test-Path -LiteralPath $cdst) -or (Get-SafeHash $cdst) -ne $cbuiltHash) {
      try { Backup-File $cdst 'Runtime\dlls\SyndicateClient.dll' } catch { }
      Copy-Item -LiteralPath $cbuilt -Destination $cdst -Force
      Write-Host '  installed Runtime\dlls\SyndicateClient.dll'
      $runtimeChanged = $true
    }
  }
}

# ---------------------------------------------------------------- controller assist (SyndicatePadAssist.dll)
# SyndicatePadAssist.dll (controller sprint by default, turbo build, edit and reset on release, custom binds) is
# loaded into every Chapter 1 / 2 game. It used to be only copied from an older install (above); it is now built from
# SyndicateClassic\launcher\launcher-source\pad-assist (SyndicatePadAssist.vcxproj), cached in
# %LOCALAPPDATA%\PassionProject\dev\pad-build, and replaces that copy. Optional: when the build fails, the copy
# from the older install stays, so nothing stops working.
Step 'Controller assist (SyndicatePadAssist.dll)'
$padSrc = Join-Path $repo 'SyndicateClassic\launcher\launcher-source\pad-assist'
$padProject = Join-Path $padSrc 'SyndicatePadAssist.vcxproj'
$pbuild = Join-Path $DevState 'pad-build'
$pbuilt = Join-Path $pbuild 'out\SyndicatePadAssist.dll'
$pdst = Join-Path $dlls 'SyndicatePadAssist.dll'
if (!(Test-Path $padProject)) {
  Write-Host '  skipped: SyndicateClassic\launcher\launcher-source\pad-assist is not in this clone' -ForegroundColor Yellow
  $summary.Add('Controller assist: SKIPPED - no source in the clone')
} elseif (!$vs) {
  Write-Host '  skipped: install Visual Studio with "Desktop development with C++" to build SyndicatePadAssist.dll.' -ForegroundColor Yellow
  $summary.Add('Controller assist: SKIPPED - Visual Studio (C++) not installed (keeping the existing copy)')
} else {
  $padHash = Get-TreeHash @($padSrc)
  $padOk = $true
  if ($Force -or !(Test-Path $pbuilt) -or $padHash -ne (Get-DevState 'pad')) {
    $pmsbuild = Join-Path $vs.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
    $ptoolset = if ([int]($vs.installationVersion.Split('.')[0]) -ge 18) { 'v145' } else { 'v143' }
    $pout = (Join-Path $pbuild 'out') + '\\'
    if (Test-Path $pout) { Remove-Item -LiteralPath $pout -Recurse -Force }
    New-Item -ItemType Directory -Path $pbuild -Force | Out-Null
    $plog = Join-Path $pbuild 'last-build-log.txt'
    if (Test-Path $plog) { Remove-Item -LiteralPath $plog -Force }
    Write-Host "  building SyndicatePadAssist.dll (toolset $ptoolset) ..."
    Write-Host "  build log: $plog"
    $msArgs = @($padProject, '/t:Build', '/p:Configuration=Release', '/p:Platform=x64', "/p:PlatformToolset=$ptoolset",
      "/p:OutDir=$pout", "/p:IntDir=$(Join-Path $pbuild 'int')\\", '/m', '/nologo', '/v:minimal')
    $code = if (Test-Path $pmsbuild) { Invoke-Native { & $pmsbuild @msArgs *>> $plog } } else { 1 }
    if (($code -ne 0 -or !(Test-Path $pbuilt)) -and (Test-Path $pmsbuild)) {
      Write-Host '  retrying with a clean rebuild ...'
      $msArgs[1] = '/t:Rebuild'
      $code = Invoke-Native { & $pmsbuild @msArgs *>> $plog }
    }
    if ($code -ne 0 -or !(Test-Path $pbuilt)) {
      if (Test-Path $plog) { Get-Content -LiteralPath $plog | Select-String -Pattern ': error ' | Select-Object -First 15 | ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red } }
      Write-Host "  Building SyndicatePadAssist.dll failed (full log: $plog). Keeping the existing copy." -ForegroundColor Yellow
      $summary.Add('Controller assist: BUILD FAILED - see ' + $plog + ' (keeping the existing copy)')
      $padOk = $false
    } else {
      Set-DevState 'pad' $padHash
      $summary.Add('Controller assist: rebuilt')
    }
  } else {
    Write-Host '  no client source changes since the last build'
    $summary.Add('Controller assist: no changes (using the cached build)')
  }
  if ($padOk) {
    $pbuiltHash = if (Test-Path -LiteralPath $pbuilt) { Get-SafeHash $pbuilt } else { $null }
    if (!$pbuiltHash) {
      Write-BlockedHelp $pbuilt
      $summary.Add('Controller assist: fresh build BLOCKED by Windows Security (see the steps above)')
    } elseif (!(Test-Path -LiteralPath $pdst) -or (Get-SafeHash $pdst) -ne $pbuiltHash) {
      try { Backup-File $pdst 'Runtime\dlls\SyndicatePadAssist.dll' } catch { }
      Copy-Item -LiteralPath $pbuilt -Destination $pdst -Force
      Write-Host '  installed Runtime\dlls\SyndicatePadAssist.dll'
      $runtimeChanged = $true
    }
  }
}

# The launcher only starts the game when every DLL matches this manifest.
if (Update-ModuleManifest $dlls) { Write-Host '  refreshed Runtime\dlls\manifest.json'; $runtimeChanged = $true }

# ---------------------------------------------------------------- launcher
Step 'Launcher (SyndicateLauncher.exe)'
$launcherSrc = Join-Path $repo 'Launcher\SyndicateLauncher'
$launcherInputs = @(Get-ChildItem -LiteralPath $launcherSrc | Where-Object { $_.Name -notin @('Runtime', 'bin', 'obj') } | ForEach-Object { $_.FullName })
$launcherHash = Get-TreeHash $launcherInputs
$exe = Join-Path $root 'SyndicateLauncher.exe'
if (!$Force -and !$runtimeChanged -and (Test-Path $exe) -and $launcherHash -eq (Get-DevState 'launcher')) {
  Write-Host '  no launcher changes since the last build'
  $summary.Add('Launcher: no changes')
} elseif (!(Get-Command dotnet -ErrorAction SilentlyContinue) -or !(Get-DotnetSdks)) {
  Write-Host '  skipped: install the .NET 8 SDK (https://dotnet.microsoft.com/download/dotnet/8.0) to build the launcher' -ForegroundColor Yellow
  $summary.Add('Launcher: SKIPPED - .NET 8 SDK not installed')
} else {
  # Build from a copy: the launcher embeds its Runtime folder, and the binaries (DLLs, lawinserver.exe)
  # are only in your Syndicate folder, not in Git.
  $stage = Join-Path $DevState 'launcher-build'
  $publish = Join-Path $DevState 'launcher-out'
  foreach ($d in $stage, $publish) { if (Test-Path $d) { Remove-Item -LiteralPath $d -Recurse -Force } }
  New-Item -ItemType Directory -Path $stage -Force | Out-Null
  & robocopy $launcherSrc (Join-Path $stage 'SyndicateLauncher') /E /XD Runtime bin obj /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw 'Copying the launcher source for the build failed.' }
  & robocopy (Join-Path $root 'Runtime') (Join-Path $stage 'SyndicateLauncher\Runtime') /E /XF *.log *.pending-* /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw 'Copying the Runtime folder for the build failed.' }
  $log = Join-Path $DevState 'launcher-build-log.txt'
  Write-Host "  build log: $log"
  $project = Join-Path $stage 'SyndicateLauncher\SyndicateLauncher.csproj'
  $code = Invoke-Native { & dotnet publish $project -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true `
    -p:DebugType=none -p:DebugSymbols=false -p:SyndicateDevBuild=true -o $publish *> $log }
  if ($code -ne 0 -or !(Test-Path (Join-Path $publish 'SyndicateLauncher.exe'))) {
    Get-Content -LiteralPath $log | Select-String -Pattern ': error ' | Select-Object -First 15 | ForEach-Object { Write-Host "  $($_.Line.Trim())" -ForegroundColor Red }
    throw "Building the launcher failed. Full log: $log"
  }
  Backup-File $exe 'SyndicateLauncher.exe'
  Copy-Item -LiteralPath (Join-Path $publish 'SyndicateLauncher.exe') -Destination $exe -Force
  $oldExe = Join-Path $root "$($oldPrefix)Launcher.exe"
  if (Test-Path -LiteralPath $oldExe) {
    Backup-File $oldExe "$($oldPrefix)Launcher.exe"
    Remove-Item -LiteralPath $oldExe -Force
    Write-Host "  removed the old $($oldPrefix)Launcher.exe (use SyndicateLauncher.exe now)"
  }
  Set-DevState 'launcher' $launcherHash
  $summary.Add('Launcher: rebuilt')
}

Write-Host ''
Write-Host 'Done.' -ForegroundColor Green
foreach ($line in $summary) { Write-Host "  $line" }
Write-Host ''
Write-Host "Start Syndicate with $exe"
