# MAKE STANDALONE RELEASE: builds the Tilted-style single encrypted Syndicate exe.
#
# It first builds the normal public package (make-public-release.ps1), then packs that complete "Syndicate"
# folder into one AES-256-GCM encrypted payload and embeds it in a self-extracting bootstrapper exe. Players
# download ONE file. On every launch the exe verifies every file against a hash list sealed inside the
# encrypted payload and re-extracts any that are missing or changed - which is exactly what happens when
# antivirus quarantines a game DLL - so an AV-eaten file self-heals instead of dead-ending at "bundle
# incomplete". Then it launches the real SyndicateLauncher.exe. Saved accounts/settings are never touched.
#
# Output: Desktop\Syndicate-Standalone-<date>.exe
#
# -UseExistingZip : skip rebuilding the package; pack today's Desktop\Syndicate-<date>.zip as-is.
# -SkipSelfTest   : skip the authenticate/extract/repair self-check (not recommended).
# -NotesFile      : release notes for GitHub (default dev\release-notes\latest.md, written from the launcher's
#                   Patch Notes in Models\PatchNotes.cs).
#
# Installed copies check the "latest release" of Sacttt/Syndicate-Launcher and d4shyb0i/Syndicate-Launcher (copies
# from before 1.2.0 only check d4shyb0i's) and take the highest version. The standalone updater installs the release's first .exe after checking its .sha256, and only when the
# tag is a higher yyyy.M.d.HHmm version than its own. So: always publish with this script, never a release that
# has no .exe, and never a tag like v1.2.0 (it would sort below v2026.x.x.x and nobody would update).
param([string]$Root = "", [string]$Version = "", [switch]$UseExistingZip, [switch]$SkipSelfTest,
      [switch]$Publish, [string]$Repo = "Sacttt/Syndicate-Launcher", [string]$NotesFile = "")
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

# 4-part numeric version (yyyy.M.d.HHmm). The minute keeps same-day builds distinct and monotonically
# increasing, which is what the self-updater compares (System.Version) against the GitHub release tag.
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = (Get-Date -Format 'yyyy.M.d.HHmm') }
$parsedVersion = $null
if (-not [Version]::TryParse($Version, [ref]$parsedVersion) -or $parsedVersion.Major -lt 2026 -or $parsedVersion.Revision -lt 0) {
  throw "Version '$Version' must look like yyyy.M.d.HHmm (for example $(Get-Date -Format 'yyyy.M.d.HHmm')). A smaller number than the last release means installed copies never update."
}
$here = $PSScriptRoot
$desktop = [Environment]::GetFolderPath('Desktop')
$zip = Join-Path $desktop ('Syndicate-' + (Get-Date -Format 'yyyy-MM-dd') + '.zip')

# 1. Build (or reuse) the full public package zip.
if ($UseExistingZip -and (Test-Path $zip)) {
  Write-Host "Using existing package: $zip"
} else {
  Step 'Building the full public package first'
  $wps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $mpr = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $here 'make-public-release.ps1'))
  if ($Root) { $mpr += @('-Root', $Root) }
  & $wps @mpr
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $zip)) { throw "The public package build did not produce $zip." }
}

# 2. Extract the "Syndicate" folder from the zip into a clean staging dir.
Step 'Extracting the package'
$work = Join-Path $env:LOCALAPPDATA 'PassionProject\dev\standalone'
$stage = Join-Path $work 'stage'
if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($zip, $stage)
$syndicateFolder = Join-Path $stage 'Syndicate'
if (-not (Test-Path (Join-Path $syndicateFolder 'SyndicateLauncher.exe'))) { throw 'The package did not contain Syndicate\SyndicateLauncher.exe.' }

# Stamp the release version so the launcher's on-screen version changes with each published update
# (MainViewModel reads Runtime\release-tag.txt and shows it at the bottom, in settings and in the about line).
Set-Content -LiteralPath (Join-Path $syndicateFolder 'Runtime\release-tag.txt') -Value $Version -NoNewline -Encoding Ascii
Write-Host "  stamped launcher version: $Version"

# 3. Pack the folder into an encrypted payload (payload.enc + PayloadKey.cs).
Step 'Encrypting the payload (AES-256-GCM)'
$payloadDir = Join-Path $work 'payload'
if (Test-Path $payloadDir) { Remove-Item -LiteralPath $payloadDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $payloadDir | Out-Null
$packer = Join-Path $here 'standalone\Packer\Packer.csproj'
& dotnet run --project $packer -c Release -- --install $syndicateFolder --out $payloadDir --version $Version
if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $payloadDir 'payload.enc'))) { throw 'Packing the payload failed.' }

# 4. Build the single bootstrapper exe with the payload embedded.
Step 'Building the single encrypted exe'
$bootProj = Join-Path $here 'standalone\Bootstrap\Bootstrap.csproj'
$publishDir = Join-Path $work 'exe'
if (Test-Path $publishDir) { Remove-Item -LiteralPath $publishDir -Recurse -Force }
& dotnet publish $bootProj -c Release -o $publishDir "-p:PayloadDir=$payloadDir" "-p:SyndicateVersion=$Version"
if ($LASTEXITCODE -ne 0) { throw 'Building the bootstrapper exe failed.' }
$exe = Join-Path $publishDir 'Syndicate.exe'
if (-not (Test-Path $exe)) { throw 'The bootstrapper build did not produce Syndicate.exe.' }

# 5. Self-test: authenticate + extract + tamper-reject + repair + cleanup in a throwaway folder.
if (-not $SkipSelfTest) {
  Step 'Self-testing the bundle'
  $testDir = Join-Path $work ('selftest-' + [Guid]::NewGuid().ToString('N'))
  $verified = "$testDir.verified.txt"
  $p = Start-Process -FilePath $exe -ArgumentList '--exercise', $testDir -Wait -PassThru -WindowStyle Hidden
  if ($p.ExitCode -ne 0 -or -not (Test-Path $verified)) { throw 'Self-test FAILED: the bundle did not verify. Not shipping it. See the standalone.log in %LOCALAPPDATA%\SyndicateStandalone.' }
  Write-Host ('  ' + (Get-Content -LiteralPath $verified -Raw).Trim())
  Remove-Item -LiteralPath $testDir -Recurse -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath $verified -Force -ErrorAction SilentlyContinue
}

# 6. Deliver.
$out = Join-Path $desktop ('Syndicate-Standalone-' + (Get-Date -Format 'yyyy-MM-dd') + '.exe')
Copy-Item -LiteralPath $exe -Destination $out -Force
Write-Host ''
Write-Host "Done: $out ($([math]::Round((Get-Item $out).Length / 1MB)) MB) - one encrypted file, self-verifying." -ForegroundColor Green
Write-Host 'Players run this single exe. It restores any file antivirus removes, then launches Syndicate.'

# 7. Publish to GitHub (so players auto-update). Uploads the exe plus a matching .sha256 the updater verifies.
if ($Publish) {
  Step 'Publishing to GitHub'
  $gh = Get-Command gh -ErrorAction SilentlyContinue
  if (-not $gh) { throw 'gh (GitHub CLI) is not installed or not on PATH, so the release cannot be published.' }
  $leaf = Split-Path $out -Leaf
  $sha = (Get-FileHash -LiteralPath $out -Algorithm SHA256).Hash.ToLower()
  $shaFile = "$out.sha256"
  Set-Content -LiteralPath $shaFile -Value ($sha + '  ' + $leaf) -NoNewline -Encoding Ascii
  $tag = 'v' + $Version
  if ([string]::IsNullOrWhiteSpace($NotesFile)) { $NotesFile = Join-Path $here 'release-notes\latest.md' }
  $notesArgs = @('--notes', 'Syndicate standalone - one encrypted, self-verifying, self-updating exe. Run it; approve the antivirus-exclusion prompt on first launch.')
  if (Test-Path -LiteralPath $NotesFile) { $notesArgs = @('--notes-file', $NotesFile) }
  $title = 'Syndicate ' + $Version
  $notesHead = if (Test-Path -LiteralPath $NotesFile) { (Get-Content -LiteralPath $NotesFile -TotalCount 1) } else { '' }
  if ($notesHead -match '^#\s+(.+)$') { $title = $Matches[1] + ' (' + $Version + ')' }
  # The exe goes up first: the updater installs the first .exe asset of the latest release.
  & gh release create $tag $out $shaFile --repo $Repo --title $title @notesArgs --latest
  if ($LASTEXITCODE -ne 0) { throw "gh release create failed (tag $tag on $Repo). Is gh authed (gh auth status)?" }
  $assets = (& gh release view $tag --repo $Repo --json assets --jq '.assets[].name') -join ', '
  Write-Host "  release assets: $assets"
  Write-Host ''
  Write-Host "Published $tag to $Repo - installed copies will auto-update to it on next launch." -ForegroundColor Green
}
