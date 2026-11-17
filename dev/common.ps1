# Shared helpers for UPDATE FROM GIT and MAKE DEV KIT (dot-sourced, Windows PowerShell 5.1).

$DevState = Join-Path $env:LOCALAPPDATA 'PassionProject\dev'
New-Item -ItemType Directory -Path $DevState -Force | Out-Null

# The DLLs the launcher checks against Runtime\dlls\manifest.json before it starts the game
# (ModuleStartup.RequiredModules in the launcher).
$RequiredModules = @('memory.dll', 'Erbium.dll', 'cobalt.dll', 'console.dll', 'Syndicate1040.dll', 'SyndicateConsoleRecovery.dll', 'SyndicateMenu.dll')

# Runs a native program without PowerShell 5.1 turning its stderr output into a script-stopping error.
# Returns the program's exit code.
function Invoke-Native([scriptblock]$command) {
  $saved = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try { & $command | Out-Null; return $LASTEXITCODE } finally { $ErrorActionPreference = $saved }
}
function Get-DotnetSdks {
  $saved = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try { return @(& dotnet --list-sdks 2>$null) } catch { return @() } finally { $ErrorActionPreference = $saved }
}

function Step($text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Cyan }

function Stop-IfRunning {
  $running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in @('SyndicateLauncher', 'TitaniumLauncher', 'Titanium Launcher V2', 'lawinserver', 'FortniteClient-Win64-Shipping') }
  if ($running) { throw 'Close Syndicate Launcher, its backend and Fortnite first, then run this again.' }
}

# A Syndicate folder is the launcher exe plus its Runtime folder (dlls + backend).
function Test-SyndicateRoot($path) {
  return ($path -and (Test-Path (Join-Path $path 'Runtime\backend\lawinserver.exe')) -and (Test-Path (Join-Path $path 'Runtime\dlls')))
}

# Dev kits made before the rename unzip to a "Titanium" folder. Renames it to "Syndicate" (same place) when
# nothing is in the way, so the old folder name never comes back. Returns the folder to use.
function Move-LegacyRoot([string]$path) {
  if (!$path -or (Split-Path $path -Leaf) -ne ('Tit' + 'anium')) { return $path }
  $target = Join-Path (Split-Path $path -Parent) 'Syndicate'
  if (Test-Path $target) { return $path }
  try {
    Rename-Item -LiteralPath $path -NewName 'Syndicate' -ErrorAction Stop
    Write-Host "  renamed your dev folder to $target"
    return $target
  } catch {
    Write-Host "  could not rename $path to Syndicate (a file in it is open?); using it as it is." -ForegroundColor Yellow
    return $path
  }
}

# The Syndicate folder: remembered after the first time, otherwise found or picked once.
function Find-SyndicateRoot([string]$Given) {
  $saved = Join-Path $DevState 'syndicate-root.txt'
  $legacySaved = Join-Path $DevState ('tit' + 'anium-root.txt')
  $candidates = @($Given)
  if (Test-Path $saved) { $candidates += (Get-Content -LiteralPath $saved -Raw).Trim() }
  elseif (Test-Path $legacySaved) { $candidates += (Get-Content -LiteralPath $legacySaved -Raw).Trim() }
  foreach ($name in @('Syndicate', ('Tit' + 'anium'))) {
    $candidates += @(
      (Join-Path $env:USERPROFILE "Desktop\$name"),
      (Join-Path $env:USERPROFILE "Documents\$name"),
      (Join-Path $env:USERPROFILE "Downloads\$name"),
      (Join-Path $env:LOCALAPPDATA $name)
    )
  }
  $found = $null
  foreach ($c in $candidates) { if (Test-SyndicateRoot $c) { $found = (Resolve-Path $c).Path; break } }
  if ($found) { $found = Move-LegacyRoot $found }
  if (!$found) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select your "Syndicate" folder (the one with SyndicateLauncher.exe and the Runtime folder). This is asked only once.'
    if ($dialog.ShowDialog() -ne 'OK') { throw 'No Syndicate folder selected.' }
    if (!(Test-SyndicateRoot $dialog.SelectedPath)) { throw 'That folder has no Runtime\backend\lawinserver.exe or Runtime\dlls. Pick the Syndicate folder from the dev kit.' }
    $found = $dialog.SelectedPath
  }
  [IO.File]::WriteAllText($saved, $found)
  return $found
}

# An existing Syndicate install to take the backend's node_modules and the Node/MongoDB
# runtimes from (they are not in Git). Returns @{ Root; Backend } or $null.
function Find-SyndicateInstall {
  $roots = New-Object System.Collections.Generic.List[string]
  foreach ($base in @([Environment]::GetFolderPath('MyDocuments'), [Environment]::GetFolderPath('Desktop'), (Join-Path $env:USERPROFILE 'Downloads'))) {
    if (!(Test-Path $base)) { continue }
    foreach ($name in @('Project Tilted', 'tiltedlegacy')) { $roots.Add((Join-Path $base $name)) }
    # e.g. an install unpacked one folder deeper on the Desktop
    foreach ($dir in (Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue)) {
      foreach ($name in @('Project Tilted', 'tiltedlegacy')) { $roots.Add((Join-Path $dir.FullName $name)) }
    }
  }
  foreach ($root in $roots) {
    foreach ($backendName in @(('Til' + 'ted-backend'), 'Syndicate-backend', 'Helix-backend-main')) {
      $backend = Join-Path $root $backendName
      if ((Test-Path (Join-Path $backend 'node_modules')) -and (Test-Path (Join-Path $backend 'package.json'))) {
        return @{ Root = $root; Backend = $backend }
      }
    }
  }
  return $null
}

# One hash for a set of folders/files (file names + contents), to skip builds when nothing changed.
function Get-TreeHash([string[]]$paths) {
  $lines = New-Object System.Collections.Generic.List[string]
  foreach ($p in $paths) {
    if (!(Test-Path $p)) { continue }
    $item = Get-Item -LiteralPath $p
    if ($item.PSIsContainer) {
      foreach ($f in (Get-ChildItem -LiteralPath $p -Recurse -File | Where-Object { $_.FullName -notmatch '\\(bin|obj|x64|\.vs)\\' } | Sort-Object FullName)) {
        $lines.Add($f.FullName.Substring($item.FullName.Length) + ':' + (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash)
      }
    } else { $lines.Add($item.Name + ':' + (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash) }
  }
  $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') } finally { $sha.Dispose() }
}

function Get-DevState([string]$name) {
  $file = Join-Path $DevState "$name.hash"
  if (Test-Path $file) { return (Get-Content -LiteralPath $file -Raw).Trim() }
  return ''
}
function Set-DevState([string]$name, [string]$value) { [IO.File]::WriteAllText((Join-Path $DevState "$name.hash"), $value) }

# SHA-256 of a file, or $null when it cannot be read (Windows Security blocks reading a file it flagged).
function Get-SafeHash([string]$path) {
  try { return (Get-FileHash -LiteralPath $path -Algorithm SHA256 -ErrorAction Stop).Hash } catch { return $null }
}
function Write-BlockedHelp([string]$path) {
  Write-Host "  Windows Security blocked $path" -ForegroundColor Yellow
  Write-Host '  To fix it:' -ForegroundColor Yellow
  Write-Host '    1. Open Windows Security > Virus & threat protection > Protection history.' -ForegroundColor Yellow
  Write-Host '    2. Find the entry for this file and choose Actions > Allow on device (or Restore).' -ForegroundColor Yellow
  Write-Host '    3. To stop it after every rebuild, add Documents\Syndicate and %LOCALAPPDATA%\PassionProject\dev under Exclusions.' -ForegroundColor Yellow
  Write-Host '    4. Run UPDATE FROM GIT.cmd again.' -ForegroundColor Yellow
}

# Rewrites Runtime\dlls\manifest.json from the DLLs actually in the folder. The launcher refuses
# to start the game when a DLL's SHA-256 does not match this file.
function Update-ModuleManifest([string]$dlls) {
  $missing = @($RequiredModules | Where-Object { !(Test-Path (Join-Path $dlls $_)) })
  if ($missing.Count -gt 0) { Write-Host "  warning: missing in Runtime\dlls: $($missing -join ', ')" -ForegroundColor Yellow }
  $entries = foreach ($name in ($RequiredModules | Where-Object { Test-Path (Join-Path $dlls $_) } | Sort-Object)) {
    $hash = Get-SafeHash (Join-Path $dlls $name)
    if (!$hash) { Write-BlockedHelp (Join-Path $dlls $name); throw "Runtime\dlls\$name cannot be read (blocked by Windows Security). Follow the steps above." }
    '  "' + $name + '": "' + $hash.ToLowerInvariant() + '"'
  }
  $json = "{`n" + (($entries) -join ",`n") + "`n}`n"
  $path = Join-Path $dlls 'manifest.json'
  $old = if (Test-Path $path) { [IO.File]::ReadAllText($path) } else { '' }
  if ($old.Replace("`r", '') -ne $json) { [IO.File]::WriteAllText($path, $json, (New-Object Text.UTF8Encoding($false))); return $true }
  return $false
}
