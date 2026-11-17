# MAKE DEV KIT: zips your Syndicate folder so another developer gets a working copy
# (launcher exe, backend with node_modules, MongoDB, game server, tools) in one download.
# Left out on purpose: accounts and logs (Runtime), .env, jwt.secret, tokens.json, ClientSettings
# saves, your game-folder paths, backups, old test launchers and Fortnite builds.
# Send the zip privately (Discord / Google Drive). Never put it on GitHub.
param([string]$Root = "")
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

if (Get-Process SyndicateLauncher -ErrorAction SilentlyContinue) { throw 'Close Syndicate Launcher (and Fortnite) first, then run this again.' }
$root = Find-SyndicateRoot $Root
Write-Host "Syndicate folder: $root"

function Test-KitFile([string]$rel) {
  $r = $rel.ToLowerInvariant()
  $name = [IO.Path]::GetFileName($r)
  $parts = $r.Split('\')
  $top = $parts[0]
  # Runtime: only the shared game data exports, never accounts, logs, travel keys or backups.
  if ($top -eq 'runtime') {
    if ($r -eq 'runtime\components-version.txt') { return $true }
    foreach ($keep in 'runtime\server\cosmetics\', 'runtime\server\challenge-data\', 'runtime\server\keychain\') { if ($r.StartsWith($keep)) { return $true } }
    return $false
  }
  if ($top -like 'launcher-test*' -or $top -eq 'fortnitegame' -or $top -eq 'engine') { return $false }
  # Secrets and personal settings.
  if ($name -in @('.env', 'jwt.secret', 'tokens.json', 'launcher.config.json', 'radmin-host.txt', 'remote-host.json')) { return $false }
  if ($name -like '.env.*' -and $name -ne '.env.example') { return $false }
  if ($r -eq 'helix-backend-main\src\local\tiltedpartybots.json') { return $false }
  if ($parts -contains 'clientsettings') { return $false }
  # Backups, old copies, logs, debug files.
  if ($name -like '*.log' -or $name -like '*.pdb' -or $name -like '*.previous-*' -or $name -like '*.before-*') { return $false }
  # Backup folders only (a file named backup-... inside node_modules is a normal package file).
  for ($i = 0; $i -lt $parts.Length - 1; $i++) { $p = $parts[$i]; if ($p -like 'backup-*' -or $p -eq 'backups' -or $p -eq 'updatebackups') { return $false } }
  # Build leftovers inside Source.
  if ($top -eq 'source') { foreach ($p in $parts) { if ($p -in @('x64', '.vs', 'bin', 'obj', 'publish', 'out')) { return $false } } }
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

Step 'Zipping'
$out = Join-Path ([Environment]::GetFolderPath('Desktop')) ("Syndicate-Dev-Kit-" + (Get-Date -Format 'yyyy-MM-dd') + ".zip")
if (Test-Path $out) { Remove-Item -LiteralPath $out -Force }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($out, 'Create')
$locked = New-Object System.Collections.Generic.List[string]
try {
  $readme = $zip.CreateEntry('READ ME - DEV KIT.txt')
  $writer = New-Object IO.StreamWriter($readme.Open())
  $writer.Write(@"
SYNDICATE - DEV KIT ($(Get-Date -Format 'yyyy-MM-dd'))

1. Unzip this anywhere (for example Documents). You get a "Project Tilted" folder.
2. Install the .NET 8 SDK: https://dotnet.microsoft.com/download/dotnet/8.0
   (and Visual Studio 2022 with "Desktop development with C++" if you want to build the game server).
3. Clone d4shyb0i/tilted-source with GitHub Desktop.
4. In the clone, double-click "UPDATE FROM GIT.cmd". The first time it asks for this
   "Project Tilted" folder; after that it remembers it.
5. Start Syndicate with Syndicate\Launcher\SyndicateLauncher.exe and choose your own Fortnite folders.

Every update after that: Pull in GitHub Desktop, then double-click UPDATE FROM GIT.cmd.
Accounts, .env and settings are created fresh on your PC; none of the host's are in this kit.
"@)
  $writer.Dispose()
  $n = 0
  foreach ($f in $files) {
    $rel = $f.FullName.Substring($root.Length).TrimStart('\')
    try { [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, ('Syndicate\' + $rel), [IO.Compression.CompressionLevel]::Optimal) }
    catch { $locked.Add($rel) }
    $n++
    if ($n % 500 -eq 0) { Write-Host ("  {0:N0} / {1:N0}" -f $n, $files.Count) }
  }
} finally { $zip.Dispose() }

Write-Host ''
Write-Host "Done: $out ($([math]::Round((Get-Item $out).Length / 1MB)) MB)" -ForegroundColor Green
if ($locked.Count -gt 0) {
  Write-Host "$($locked.Count) file(s) were in use and left out (close Syndicate and its services, then run again):" -ForegroundColor Yellow
  $locked | Select-Object -First 15 | ForEach-Object { Write-Host "  $_" }
}
Write-Host 'Send it privately (Discord / Google Drive). Do not upload it to GitHub.'
