# MAKE PUBLIC RELEASE: builds the obfuscated, non-dev launcher and zips a player-ready "Syndicate" folder
# (launcher exe + Runtime + backend + Dependencies) to your Desktop, ready to send to someone.
#
# Difference from MAKE DEV KIT: the launcher is built WITHOUT -p:SyndicateDevBuild, so it is a Release build.
# That means no DEV LAUNCHER badge, no Studio button, and the Obfuscar step runs (rename + string encryption +
# SuppressIldasm + the anti-debug guard). The dev-only Studio folder is left out of the package.
#
# Player profiles, lockers and settings live in %APPDATA%\SyndicateLauncher, not in the Syndicate folder, so
# none of yours are ever in the package. Logs, backups, per-PC server state, secrets and Fortnite builds are
# left out too. Send the zip privately (Discord / Google Drive). Never put it on GitHub.
param([string]$Root = "")
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

Stop-IfRunning
$root = Find-SyndicateRoot $Root
$repo = Split-Path $PSScriptRoot -Parent
Write-Host "Syndicate folder: $root"

# Build the changed source (DLLs, backend) and refresh the manifest so the package cannot ship stale binaries.
Step 'Updating and building from this clone'
$windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
& $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'update-from-git.ps1') -Root $root
if ($LASTEXITCODE -ne 0) { throw 'Updating/building failed. No public release was created.' }

$missingModules = @($RequiredModules | Where-Object { !(Test-Path -LiteralPath (Join-Path $root "Runtime\dlls\$_")) })
if ($missingModules.Count -gt 0) { throw "Cannot package an incomplete release. Missing Runtime\dlls: $($missingModules -join ', ')." }
if (Update-ModuleManifest (Join-Path $root 'Runtime\dlls')) { Write-Host '  refreshed Runtime\dlls\manifest.json' }

# Build the public (non-dev) launcher: self-contained single file, obfuscated. No -p:SyndicateDevBuild here, so
# the csproj defines no DEV_BUILD and the Release-only Obfuscar step runs. The csproj errors the build if
# obfuscation did not run, so a launcher that comes out of here is always obfuscated.
Step 'Building the public (obfuscated) launcher'
$project = Join-Path $repo 'Launcher\SyndicateLauncher\SyndicateLauncher.csproj'
$publishDir = Join-Path $DevState 'public-build'
if (Test-Path $publishDir) { Remove-Item -LiteralPath $publishDir -Recurse -Force }
$code = Invoke-Native { & dotnet publish $project -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true `
    -p:DebugType=none -p:DebugSymbols=false -o $publishDir }
$releaseExe = Join-Path $publishDir 'SyndicateLauncher.exe'
if ($code -ne 0 -or !(Test-Path -LiteralPath $releaseExe)) {
  throw 'The public launcher build failed (scroll up for the first error). If it mentions obfuscation, run "cd Launcher" then "dotnet tool restore" and try again.'
}
Write-Host "  built $releaseExe ($([math]::Round((Get-Item $releaseExe).Length / 1MB)) MB, obfuscated)"

# ---- sign the EXE -------------------------------------------------------
Step 'Signing the launcher EXE'

# Locate signtool (highest Windows SDK version, x64)
$signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $signtool) {
    Write-Host '  signtool.exe not found — EXE will be unsigned.' -ForegroundColor Yellow
    Write-Host '  Install the Windows 10/11 SDK or run MAKE PUBLIC RELEASE from a machine that has it.' -ForegroundColor Yellow
} else {
    # Load .env next to build.bat (Launcher\.env)
    $envFile = Join-Path $PSScriptRoot '..\Launcher\.env'
    $pfxPass = $env:SYNDICATE_PFX_PASS
    $thumbprint = $env:SYNDICATE_CERT_THUMBPRINT
    if (Test-Path $envFile) {
        foreach ($line in (Get-Content $envFile)) {
            if ($line -match '^SYNDICATE_PFX_PASS=(.+)$')        { $pfxPass     = $Matches[1] }
            if ($line -match '^SYNDICATE_CERT_THUMBPRINT=(.+)$') { $thumbprint  = $Matches[1] }
        }
    }

    $pfxPath  = Join-Path $PSScriptRoot '..\Launcher\syndicate.pfx'
    $tsUrl    = 'http://timestamp.digicert.com'
    $signed   = $false

    # Path A: cert thumbprint in Windows store
    if ($thumbprint) {
        Write-Host "  signing with store certificate ($($thumbprint.Substring(0,8))...)"
        $code = Invoke-Native { & $signtool sign /sha1 $thumbprint /fd sha256 /tr $tsUrl /td sha256 /v $releaseExe }
        if ($code -eq 0) { $signed = $true } else { Write-Host '  store-cert signing failed.' -ForegroundColor Yellow }
    }

    # Path B: PFX file
    if (-not $signed -and (Test-Path $pfxPath)) {
        if (-not $pfxPass) {
            Write-Host '  syndicate.pfx found but SYNDICATE_PFX_PASS is not set in Launcher\.env — skipping signing.' -ForegroundColor Yellow
        } else {
            Write-Host '  signing with syndicate.pfx...'
            $code = Invoke-Native { & $signtool sign /f $pfxPath /p $pfxPass /fd sha256 /tr $tsUrl /td sha256 /v $releaseExe }
            if ($code -eq 0) { $signed = $true } else { Write-Host '  PFX signing failed.' -ForegroundColor Yellow }
        }
    }

    if ($signed) {
        $code2 = Invoke-Native { & $signtool verify /pa /v $releaseExe }
        if ($code2 -eq 0) {
            Write-Host '  Signed and verified OK.' -ForegroundColor Green
        } else {
            Write-Host '  Signed but verification returned a warning — check signtool output above.' -ForegroundColor Yellow
        }
    } else {
        Write-Host '  EXE is unsigned. Players will see a SmartScreen warning on download.' -ForegroundColor Yellow
        Write-Host '  Add syndicate.pfx + SYNDICATE_PFX_PASS to Launcher\.env to sign automatically.' -ForegroundColor Yellow
    }
}

# What goes in the package. Like the dev kit, minus the dev-only Studio folder and the dev launcher exe
# (the obfuscated release exe is added separately).
function Test-ReleaseFile([string]$rel) {
  $r = $rel.ToLowerInvariant()
  $name = [IO.Path]::GetFileName($r)
  $parts = $r.Split('\')
  if ($name -ieq 'syndicatelauncher.exe') { return $false }      # the obfuscated release exe is added on its own
  if ($name -like 'titanium*' -and $name -notlike '*.cjs') { return $false }  # legacy Titanium launcher/pdb/config - never ship it (keep titanium-*.cjs; lawinserver needs them)
  if ($r.StartsWith('runtime\studio\') -or $r -eq 'runtime\studio') { return $false }  # Studio is dev-only
  if ($parts[0] -in @('_backups', 'fortnitegame', 'engine')) { return $false }
  if ($r.StartsWith('runtime\server\') -or $r.StartsWith('syndicate-backend\logs\')) { return $false }
  for ($i = 0; $i -lt $parts.Length - 1; $i++) { if ($parts[$i] -like 'backup*' -or $parts[$i] -eq 'logs') { return $false } }
  if ($name -like '*.log' -or $name -like '*.pdb' -or $name -like '*.pending-*' -or $name -like '*.zip' -or $name -like '*.rar') { return $false }
  if ($name -in @('.env', 'settings.json', 'tokens.json', 'jwt.secret', 'legacy-profile-owner.txt', 'profile-owner.json')) { return $false }
  if ($parts -contains 'players') { return $false }
  return $true
}

Step 'Collecting files'
$files = New-Object System.Collections.Generic.List[object]
$skipped = 0
foreach ($f in (Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue)) {
  $rel = $f.FullName.Substring($root.Length).TrimStart('\')
  if (Test-ReleaseFile $rel) { $files.Add($f) } else { $skipped++ }
}
$bytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ("  {0:N0} files ({1:N0} MB) plus the launcher; {2:N0} dev, private or unneeded files left out." -f $files.Count, ($bytes / 1MB), $skipped)

Step 'Zipping'
$out = Join-Path ([Environment]::GetFolderPath('Desktop')) ('Syndicate-' + (Get-Date -Format 'yyyy-MM-dd') + '.zip')
if (Test-Path $out) { Remove-Item -LiteralPath $out -Force }
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($out, 'Create')
$locked = New-Object System.Collections.Generic.List[string]
try {
  $readme = $zip.CreateEntry('READ ME - SYNDICATE.txt')
  $writer = New-Object IO.StreamWriter($readme.Open())
  $writer.Write(@"
SYNDICATE ($(Get-Date -Format 'yyyy-MM-dd'))

1. UNZIP this whole folder somewhere normal (Desktop or Documents). Extract it - do not run anything
   from inside the ZIP preview.
2. Run "SETUP - Run Me First.cmd" and click YES when Windows asks for admin.
   It adds the antivirus exclusion (so Windows does not delete Syndicate's files), makes a Desktop
   shortcut, and starts the launcher for you.
3. Pick a season and press Play.

Notes:
- Keep the whole "Syndicate" folder together.
- Windows SmartScreen may warn (unknown publisher): click More info, then Run anyway.
- If Windows Security already removed a file: run "SETUP - Run Me First.cmd" first, then unzip this
  into the SAME folder again so the files come back into the now-excluded folder.
"@)
  $writer.Dispose()

  # A "Run Me First" setup: self-elevates, adds a Windows Defender folder exclusion so the mod DLLs are
  # not quarantined, makes a Desktop shortcut, and launches. This is the single biggest reduction in the
  # "file contains a virus" / "files don't match" / "bundle incomplete" support load.
  $setup = $zip.CreateEntry('Syndicate\SETUP - Run Me First.cmd')
  $sw = New-Object IO.StreamWriter($setup.Open(), [Text.Encoding]::ASCII)
  $sw.Write(@'
@echo off
title Syndicate Setup
net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Asking for administrator access to add the antivirus exclusion...
  powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
  exit /b
)
set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"
echo Adding a Windows Security exclusion for this folder so Syndicate's files are not removed...
powershell -NoProfile -Command "try { Add-MpPreference -ExclusionPath '%HERE%' -ErrorAction Stop; Write-Host '  exclusion added.' } catch { Write-Host '  could not add it automatically. Add this folder to Windows Security exclusions manually:'; Write-Host '  %HERE%' }"
echo Creating a Desktop shortcut...
powershell -NoProfile -Command "$w=New-Object -ComObject WScript.Shell; $s=$w.CreateShortcut([IO.Path]::Combine([Environment]::GetFolderPath('Desktop'),'Syndicate.lnk')); $s.TargetPath='%HERE%\SyndicateLauncher.exe'; $s.WorkingDirectory='%HERE%'; $s.Save()"
echo.
echo Done. Starting Syndicate...
start "" "%HERE%\SyndicateLauncher.exe"
exit /b
'@)
  $sw.Dispose()
  [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $releaseExe, 'Syndicate\SyndicateLauncher.exe', [IO.Compression.CompressionLevel]::Optimal)
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
Write-Host 'The launcher in this package is the obfuscated Release build (no DEV badge, no Studio).'
Write-Host 'Send it privately (Discord / Google Drive). Do not upload it to GitHub.'
