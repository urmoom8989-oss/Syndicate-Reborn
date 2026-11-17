# PUBLISH PUBLIC RELEASE: builds the obfuscated, non-dev launcher package (via make-public-release.ps1)
# and uploads the resulting ZIP to a GitHub Release using the gh CLI.
#
# ASSET: this uploads the full working package as a ZIP (Desktop\Syndicate-<date>.zip): the obfuscated launcher
# exe plus the complete Runtime, Node backend and Dependencies - everything needed to run, like the dev kit, so
# every season works (not just 10.40). Players unzip the "Syndicate" folder and run the exe inside it.
# NOTE: this package contains the Syndicate-backend Node source, whose syndicateLegacyXmpp.js has a built-in key.
# A fully-working offline build cannot avoid shipping it (the backend runs on the player's PC). On a public repo
# that key is exposed; rotate it post-launch if that matters.
#
# Requirements: GitHub CLI (gh) installed and signed in.
#   winget install --id GitHub.cli
#   gh auth login
#
# Discord: after publishing, it posts the release link to a Discord webhook. The webhook is a secret and is
# NOT stored in the repo. It is read from (in order): -DiscordWebhook, the SYNDICATE_DISCORD_WEBHOOK env var,
# or %LOCALAPPDATA%\PassionProject\dev\discord-webhook.txt. Add -PingEveryone to @everyone. Add -NoDiscord to skip.
#
# Examples:
#   PUBLISH PUBLIC RELEASE.cmd
#   PUBLISH PUBLIC RELEASE.cmd -Tag v2026-11-15 -Draft
#   PUBLISH PUBLIC RELEASE.cmd -PingEveryone
#   PUBLISH PUBLIC RELEASE.cmd -DiscordWebhook "https://discord.com/api/webhooks/..." -PingEveryone
#   PUBLISH PUBLIC RELEASE.cmd -Repo myuser/Passion-Project -NoDiscord
param(
  [string]$Root = "",
  [string]$Repo = "d4shyb0i/Syndicate-Launcher",
  [string]$Tag = "",
  [string]$Title = "",
  [string]$Notes = "",
  [switch]$Draft,
  [string]$DiscordWebhook = "",
  [string]$DiscordMessage = "",
  [switch]$PingEveryone,
  [switch]$NoDiscord
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Split-Path $PSScriptRoot -Parent

# 1. gh must be installed and authenticated before we spend time building.
Step 'Checking GitHub CLI'
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
  throw 'GitHub CLI (gh) is not installed. Install it with:  winget install --id GitHub.cli   then run:  gh auth login'
}
& gh auth status 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
  throw 'GitHub CLI is not signed in. Run:  gh auth login   then try again.'
}

# 2. Work out which repo we are publishing to (default: this clone's origin remote).
if ([string]::IsNullOrWhiteSpace($Repo)) {
  $originUrl = (& git -C $repoRoot remote get-url origin 2>$null)
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($originUrl)) {
    throw 'Could not read the origin remote of this clone. Pass the repo explicitly, e.g. -Repo owner/Passion-Project'
  }
  if ($originUrl -match 'github\.com[/:]+([^/]+)/([^/.]+)') { $Repo = "$($Matches[1])/$($Matches[2])" }
  else { throw "Could not parse a GitHub owner/repo from origin URL: $originUrl. Pass -Repo owner/name." }
}
Write-Host "  target repo: $Repo"

# 3. Refuse to publish to a PUBLIC repo while the backend key is in the package.
$visibility = (& gh repo view $Repo --json visibility --jq '.visibility' 2>$null)
if ($LASTEXITCODE -ne 0) {
  throw "Could not read $Repo from GitHub (does it exist and do you have access?). gh said nothing usable."
}
Write-Host "  repo visibility: $visibility"
if ($visibility -ieq 'PUBLIC') {
  Write-Host ''
  Write-Host "WARNING: $Repo is PUBLIC, and this full package INCLUDES the Syndicate-backend Node source," -ForegroundColor Yellow
  Write-Host 'whose syndicateLegacyXmpp.js has a built-in key. Publishing it to a public repo exposes that key.' -ForegroundColor Yellow
  Write-Host 'A fully-working offline build cannot avoid shipping the backend (it runs on the player PC). Proceeding.' -ForegroundColor Yellow
}

# 3b. Stamp the release tag into the launcher's Runtime BEFORE building, so the built exe knows its own version.
#     On startup the launcher reads Runtime\release-tag.txt and, if a newer tag exists on GitHub, offers the
#     update (Services\UpdateChecker.cs). Done here so the tag is baked into the package.
if ([string]::IsNullOrWhiteSpace($Tag)) { $Tag = 'v' + (Get-Date -Format 'yyyy-MM-dd-HHmmss') }
Step 'Stamping release tag into the launcher'
$tagFile = Join-Path $repoRoot 'Launcher\SyndicateLauncher\Runtime\release-tag.txt'
New-Item -ItemType Directory -Force -Path (Split-Path $tagFile) | Out-Null
Set-Content -LiteralPath $tagFile -Value $Tag -NoNewline -Encoding Ascii
Write-Host "  release tag: $Tag"

# 4. Build the full working package (make-public-release.ps1): builds + obfuscates the launcher and zips the
#    complete "Syndicate" folder (exe + Runtime + backend + Dependencies) to the Desktop. This is the dev-kit-
#    like package - everything needed to run - so every season works, not just 10.40.
Step 'Building the full public release package'
$windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$mprArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'make-public-release.ps1'))
if (-not [string]::IsNullOrWhiteSpace($Root)) { $mprArgs += @('-Root', $Root) }
& $windowsPowerShell @mprArgs
if ($LASTEXITCODE -ne 0) { throw 'Building the release package failed. Nothing was published.' }

# 5. Locate the ZIP make-public-release.ps1 just wrote (Desktop\Syndicate-<today>.zip).
$asset = Join-Path ([Environment]::GetFolderPath('Desktop')) ('Syndicate-' + (Get-Date -Format 'yyyy-MM-dd') + '.zip')
if (-not (Test-Path -LiteralPath $asset)) {
  throw "Could not find the built package at $asset. Did the build finish? Nothing was published."
}
Write-Host "  asset: $asset ($([math]::Round((Get-Item $asset).Length / 1MB)) MB)"

# 6. Defaults for the release.
if ([string]::IsNullOrWhiteSpace($Tag)) { $Tag = 'v' + (Get-Date -Format 'yyyy-MM-dd-HHmmss') }
if ([string]::IsNullOrWhiteSpace($Title)) { $Title = 'Syndicate ' + (Get-Date -Format 'yyyy-MM-dd') }
if ([string]::IsNullOrWhiteSpace($Notes)) { $Notes = 'Public Syndicate launcher build. Unzip and run SyndicateLauncher.exe.' }

# 7. Create the release (or add/replace the asset if the tag already exists).
Step "Publishing release $Tag to $Repo"
$ghArgs = @('release', 'create', $Tag, $asset, '--repo', $Repo, '--title', $Title, '--notes', $Notes)
if ($Draft) { $ghArgs += '--draft' }
& gh @ghArgs
if ($LASTEXITCODE -ne 0) {
  Write-Host '  release create failed (tag may already exist); trying to upload the asset to the existing release...' -ForegroundColor Yellow
  & gh release upload $Tag $asset --repo $Repo --clobber
  if ($LASTEXITCODE -ne 0) { throw 'Publishing failed. See the gh output above.' }
}

$url = (& gh release view $Tag --repo $Repo --json url --jq '.url' 2>$null)
Write-Host ''
if (-not [string]::IsNullOrWhiteSpace($url)) { Write-Host "Done. Release: $url" -ForegroundColor Green }
else { Write-Host 'Done. Release published.' -ForegroundColor Green }
Write-Host 'Reminder: if this repo is private, downloaders need repo access or an authenticated link.'

# 8. Optional: announce to Discord via a webhook. The webhook URL is a secret, so it is NOT stored in the repo.
#    It is read, in order of preference, from: -DiscordWebhook, the env var SYNDICATE_DISCORD_WEBHOOK, or the
#    untracked file %LOCALAPPDATA%\PassionProject\dev\discord-webhook.txt. Pass -NoDiscord to skip announcing.
if (-not $NoDiscord) {
  $webhook = $DiscordWebhook
  if ([string]::IsNullOrWhiteSpace($webhook)) { $webhook = $env:SYNDICATE_DISCORD_WEBHOOK }
  if ([string]::IsNullOrWhiteSpace($webhook)) {
    $webhookFile = Join-Path $env:LOCALAPPDATA 'PassionProject\dev\discord-webhook.txt'
    if (Test-Path -LiteralPath $webhookFile) { $webhook = (Get-Content -LiteralPath $webhookFile -Raw).Trim() }
  }

  if ([string]::IsNullOrWhiteSpace($webhook)) {
    Write-Host ''
    Write-Host 'No Discord webhook found, so nothing was posted. To auto-announce next time, do one of:' -ForegroundColor Yellow
    Write-Host '  - pass -DiscordWebhook "https://discord.com/api/webhooks/..."' -ForegroundColor Yellow
    Write-Host '  - set the SYNDICATE_DISCORD_WEBHOOK environment variable' -ForegroundColor Yellow
    Write-Host '  - save the URL in %LOCALAPPDATA%\PassionProject\dev\discord-webhook.txt' -ForegroundColor Yellow
  }
  elseif (-not ($webhook -match '^https://(discord\.com|discordapp\.com)/api/webhooks/')) {
    Write-Host ''
    Write-Host 'The Discord webhook does not look like a discord.com webhook URL; skipping the announcement.' -ForegroundColor Yellow
  }
  else {
    Step 'Announcing on Discord'
    if ([string]::IsNullOrWhiteSpace($DiscordMessage)) {
      $link = if (-not [string]::IsNullOrWhiteSpace($url)) { $url } else { "the GitHub releases page" }
      $DiscordMessage = "Syndicate $Tag is live! Download: $link"
    }
    $content = if ($PingEveryone) { "@everyone " + $DiscordMessage } else { $DiscordMessage }
    $payload = @{ content = $content }
    if ($PingEveryone) { $payload['allowed_mentions'] = @{ parse = @('everyone') } }
    $json = $payload | ConvertTo-Json -Depth 5 -Compress
    try {
      Invoke-RestMethod -Method Post -Uri $webhook -ContentType 'application/json; charset=utf-8' -Body $json | Out-Null
      Write-Host '  posted to Discord.' -ForegroundColor Green
    } catch {
      Write-Host "  Discord announcement failed: $($_.Exception.Message)" -ForegroundColor Yellow
      Write-Host '  (The release itself was published; only the Discord post failed.)' -ForegroundColor Yellow
    }
  }
}
