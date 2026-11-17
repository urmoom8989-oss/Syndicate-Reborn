# MAKE DEV KIT: updates/builds from this clone, then zips your Syndicate folder (launcher exe,
# Runtime with the DLLs and lawinserver.exe) in one download. These binaries are not in Git.
# Launcher profiles, lockers and settings live in %APPDATA%\SyndicateLauncher, not in the Syndicate
# folder, so they are never in the kit. Logs, backups and Fortnite builds are left out too.
# Send the zip privately (Discord / Google Drive). Never put it on GitHub.
param([string]$Root = "")
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Stop-IfRunning
$root = Find-SyndicateRoot $Root
Write-Host "Syndicate folder: $root"

# Build changed source before packaging so the kit cannot silently ship an older launcher.
Step 'Updating and building this dev kit'
$windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
& $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'update-from-git.ps1') -Root $root
if ($LASTEXITCODE -ne 0) { throw 'Updating/building failed. No dev kit was created.' }

# The updater can skip compilation when an SDK is missing. Refuse a stale launcher in that case.
$launcherSource = Join-Path (Split-Path $PSScriptRoot -Parent) 'Launcher\SyndicateLauncher'
$launcherInputs = @(Get-ChildItem -LiteralPath $launcherSource | Where-Object { $_.Name -notin @('Runtime', 'bin', 'obj') } | ForEach-Object { $_.FullName })
if (!(Test-Path -LiteralPath (Join-Path $root 'SyndicateLauncher.exe')) -or
    (Get-TreeHash $launcherInputs) -ne (Get-DevState 'launcher')) {
  throw 'The launcher was not built from the current source. Install the .NET 8 SDK and try again. No dev kit was created.'
}
$missingModules = @($RequiredModules | Where-Object { !(Test-Path -LiteralPath (Join-Path $root "Runtime\dlls\$_")) })
if ($missingModules.Count -gt 0) {
  throw "Cannot package an incomplete kit. Missing Runtime\dlls: $($missingModules -join ', ')."
}

# Make sure the kit's manifest matches its DLLs, or the launcher would refuse to start the game.
if (Update-ModuleManifest (Join-Path $root 'Runtime\dlls')) { Write-Host '  refreshed Runtime\dlls\manifest.json' }

function Test-KitFile([string]$rel) {
  $r = $rel.ToLowerInvariant()
  $name = [IO.Path]::GetFileName($r)
  $parts = $r.Split('\')
  # Backups, logs, debug files, half-written files.
  if ($parts[0] -in @('_backups', 'fortnitegame', 'engine')) { return $false }
  # Backend runtime state (matchmaker/server files) is per PC.
  if ($r.StartsWith('runtime\server\') -or $r.StartsWith('syndicate-backend\logs\')) { return $false }
  for ($i = 0; $i -lt $parts.Length - 1; $i++) { if ($parts[$i] -like 'backup*' -or $parts[$i] -eq 'logs') { return $false } }
  if ($name -like '*.log' -or $name -like '*.pdb' -or $name -like '*.pending-*' -or $name -like '*.zip' -or $name -like '*.rar') { return $false }
  # Secrets and personal files, in case any ever end up in the folder.
  if ($name -in @('.env', 'settings.json', 'tokens.json', 'jwt.secret', 'legacy-profile-owner.txt', 'profile-owner.json')) { return $false }
  if ($parts -contains 'players') { return $false }
  return $true
}

Step 'Collecting files'
$files = New-Object System.Collections.Generic.List[object]
$skipped = 0
foreach ($f in (Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue)) {
  $rel = $f.FullName.Substring($root.Length).TrimStart('\')
  if (Test-KitFile $rel) { $files.Add($f) } else { $skipped++ }
}
$bytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ("  {0:N0} files ({1:N0} MB) go in; {2:N0} private or unneeded files left out." -f $files.Count, ($bytes / 1MB), $skipped)
if (!($files | Where-Object { $_.Name -ieq 'SyndicateLauncher.exe' })) {
  Write-Host '  note: there is no SyndicateLauncher.exe in this folder. The other developer will need the .NET 8 SDK and' -ForegroundColor Yellow
  Write-Host '  UPDATE FROM GIT.cmd to build it.' -ForegroundColor Yellow
}

Step 'Zipping'
$out = Join-Path ([Environment]::GetFolderPath('Desktop')) ('Syndicate-Dev-Kit-' + (Get-Date -Format 'yyyy-MM-dd') + '.zip')
if (Test-Path $out) { Remove-Item -LiteralPath $out -Force }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($out, 'Create')
$locked = New-Object System.Collections.Generic.List[string]
try {
  $readme = $zip.CreateEntry('READ ME - DEV KIT.txt')
  $writer = New-Object IO.StreamWriter($readme.Open())
  $writer.Write(@"
SYNDICATE (PASSION PROJECT) - DEV KIT ($(Get-Date -Format 'yyyy-MM-dd'))

1. Unzip this anywhere (for example Documents). You get a "Syndicate" folder.
   It already includes the Syndicate server/client DLLs (Erbium, cobalt, client mods),
   so you do not need a Syndicate install.
2. Install the .NET 8 SDK: https://dotnet.microsoft.com/download/dotnet/8.0
   and Visual Studio with "Desktop development with C++" to build the game server.
3. Clone Sacttt/Passion-Project with GitHub Desktop.
4. In the clone, double-click "UPDATE FROM GIT.cmd". The first time it asks for this
   "Syndicate" folder; after that it remembers it.
5. Start Syndicate with Syndicate\SyndicateLauncher.exe.

Every update after that: Pull in GitHub Desktop, then double-click UPDATE FROM GIT.cmd.
MAKE DEV KIT.cmd also runs the updater/build automatically before packaging.
Your launcher profiles and settings are created fresh on your PC; none of the host's are in this kit.
"@)
  $writer.Dispose()
  $n = 0
  foreach ($f in $files) {
    $rel = $f.FullName.Substring($root.Length).TrimStart('\')
    try { [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, ('Syndicate\' + $rel), [IO.Compression.CompressionLevel]::Optimal) }
    catch { $locked.Add($rel) }
    $n++
    if ($n % 100 -eq 0) { Write-Host ("  {0:N0} / {1:N0}" -f $n, $files.Count) }
  }
} finally { $zip.Dispose() }

Write-Host ''
Write-Host "Done: $out ($([math]::Round((Get-Item $out).Length / 1MB)) MB)" -ForegroundColor Green
if ($locked.Count -gt 0) {
  Write-Host "$($locked.Count) file(s) were in use and left out (close Syndicate and Fortnite, then run again):" -ForegroundColor Yellow
  $locked | Select-Object -First 15 | ForEach-Object { Write-Host "  $_" }
}
Write-Host 'Send it privately (Discord / Google Drive). Do not upload it to GitHub.'
