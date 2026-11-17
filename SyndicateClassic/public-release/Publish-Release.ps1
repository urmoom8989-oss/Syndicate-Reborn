<#
  Syndicate - publish a public release in one step.

  Takes the EXE that MAKE PUBLIC RELEASE already built (Desktop\Project-Syndicate-Public-Release),
  then:
    1. Writes RELEASE-NOTES.md and latest.json from public-release\release-notes.json.
    2. Creates the GitHub Release on Sacttt/ProjectTilted (the public repo) with the EXE, its
       .sha256 and latest.json attached. Players get it the next time they open Syndicate.
    3. Posts a short, easy-to-read announcement to Discord with the download link and the EXE,
       showing what changed.

  Nothing is published or posted without you confirming. Use -Preview to see both first.

  The notes come only from release-notes.json - this script never invents changes or versions.

  The Discord webhook is read from a file on THIS PC only, never from the repo:
      %APPDATA%\SyndicateClassic-Release\discord-release-webhook.txt
  Anyone with that URL can post in your channel, so keep it off GitHub and out of chat.

  Examples:
    .\Publish-Release.ps1 -Preview                 # show the GitHub notes + Discord post, publish nothing
    .\Publish-Release.ps1                           # the version MAKE PUBLIC RELEASE built last, asks before each step
    .\Publish-Release.ps1 -Version 0.4.2
    .\Publish-Release.ps1 -SkipDiscord             # GitHub release only
    .\Publish-Release.ps1 -SkipGithub              # Discord announcement only (release already exists)
#>
param(
    [string]$Version = "",
    [string]$Repo = "Sacttt/ProjectTilted",
    [string]$Mention = "",            # e.g. "@everyone" or "<@&ROLE_ID>"; left blank by default
    [switch]$SkipGithub,
    [switch]$SkipDiscord,
    [switch]$Preview,
    [switch]$Yes                      # skip the y/n confirmations (still never runs under -Preview)
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($Repo -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw "Repo must look like owner/name (got '$Repo')." }

# ---- Notes: real history only ------------------------------------------------
$notesFile = Join-Path $here "release-notes.json"
if (!(Test-Path $notesFile)) { throw "Missing $notesFile. Add an entry for this version before publishing." }
# Windows PowerShell 5.1 passes a JSON array down the pipeline as one object; unwrap each entry.
$notes = @(Get-Content $notesFile -Raw -Encoding UTF8 | ConvertFrom-Json | ForEach-Object { $_ })
# With no -Version, publish the version MAKE PUBLIC RELEASE built last (Desktop\Project-Syndicate-Public-Release\
# last-version.txt), not just the newest notes entry, so an old version is never offered again.
if (-not $Version) {
    $lastBuilt = Join-Path (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Project-Syndicate-Public-Release') 'last-version.txt'
    if (Test-Path $lastBuilt) { $Version = (Get-Content -LiteralPath $lastBuilt -Raw).Trim().TrimStart('v','V') }
}
$entry = if ($Version) { $notes | Where-Object { $_.version -eq $Version } | Select-Object -First 1 } else { $notes[0] }
if (-not $entry) { throw "No notes for version '$Version' in public-release\release-notes.json (newest there is $($notes[0].version)). Add a '$Version' entry at the top first - this script will not make them up." }
$Version = "$($entry.version)".Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "release-notes.json has version '$Version'; it must look like 0.4.2." }
if ($Version -eq "0.0.0") { throw "release-notes.json still holds the example entry (0.0.0). Replace it with the real release before publishing." }
$tag = "v$Version"
$new   = @($entry.new)   | Where-Object { "$_".Trim() }
$fixed = @($entry.fixed) | Where-Object { "$_".Trim() }

# ---- The EXE that MAKE PUBLIC RELEASE built ----------------------------------
$deliver = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Project-Syndicate-Public-Release'
$exeName = "Project-Syndicate-v$Version.exe"
$exe = Join-Path $deliver $exeName
if (!(Test-Path $exe)) {
    throw @"
Couldn't find $exeName in $deliver.
Run MAKE PUBLIC RELEASE first (version $Version), then run this script again.
"@
}
$shaFile = "$exe.sha256"
if (!(Test-Path $shaFile)) {
    $h = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath $shaFile -Value "$h  $exeName" -Encoding ASCII
}
$sha = ((Get-Content -LiteralPath $shaFile -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
$mb = [math]::Round((Get-Item $exe).Length / 1MB, 1)
$date = if ($entry.date -and "$($entry.date)" -ne "YYYY-MM-DD") { "$($entry.date)" } else { (Get-Date -Format 'yyyy-MM-dd') }

# Direct links on the finished GitHub release.
$releasePage = "https://github.com/$Repo/releases/tag/$tag"
$exeUrl      = "https://github.com/$Repo/releases/download/$tag/$exeName"

# ---- RELEASE-NOTES.md + latest.json ------------------------------------------
$md = @()
$md += "# Syndicate $tag"
$md += ""
$md += "_$($date)_"
if ($new)   { $md += ""; $md += "## What's new"; $md += ($new   | ForEach-Object { "- $_" }) }
if ($fixed) { $md += ""; $md += "## Fixed";      $md += ($fixed | ForEach-Object { "- $_" }) }
$md += ""
$md += "## How to get it"
$md += "- Already have Syndicate? Just reopen the launcher - it updates itself. Or press **Check for updates** (Settings, bottom left)."
$md += "- New here? Download **$exeName** below and run it."
$md += ""
$md += "SHA-256 ``$sha``"
$mdText = ($md -join "`n")
$notesMd = Join-Path $deliver "RELEASE-NOTES.md"
[IO.File]::WriteAllText($notesMd, $mdText, (New-Object Text.UTF8Encoding $false))

$latest = [ordered]@{
    version = $Version; tag = $tag; date = $date
    file = $exeName; size = (Get-Item $exe).Length; sha256 = $sha
    url = $exeUrl; releasePage = $releasePage
    new = $new; fixed = $fixed
}
$latestFile = Join-Path $deliver "latest.json"
[IO.File]::WriteAllText($latestFile, ($latest | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding $false))

# ---- Discord embed (built now so -Preview can show it) -----------------------
function Join-Lines($items, $bullet) {
    $t = (@($items) | ForEach-Object { "$bullet $_" }) -join "`n"
    if ($t.Length -gt 1020) { $t = $t.Substring(0, 1000).TrimEnd() + "`n...and more" }
    if (-not $t) { $t = "-" }
    return $t
}
$fields = @()
if ($new)   { $fields += @{ name = "What's new"; value = (Join-Lines $new   ">"); inline = $false } }
if ($fixed) { $fields += @{ name = "Fixed";      value = (Join-Lines $fixed ">"); inline = $false } }
$fields += @{ name = "Download"; value = "**[Download Syndicate $tag]($exeUrl)**`n[All files / release page]($releasePage)"; inline = $false }
$fields += @{ name = "Size"; value = "$mb MB"; inline = $true }
$fields += @{ name = "How to update"; value = "Already installed? Open Syndicate - it updates itself. New? Download and run the EXE."; inline = $false }
$embed = @{
    title = "Syndicate $tag is out!"
    description = "A new update just dropped. Here's what changed."
    url = $releasePage
    color = 0xF5B301
    fields = $fields
    footer = @{ text = "Syndicate  -  $tag" }
    timestamp = ([DateTime]::UtcNow.ToString("o"))
}

# ---- Preview -----------------------------------------------------------------
Write-Host ""
Write-Host "==== GitHub release preview ($Repo $tag) ====" -ForegroundColor Cyan
Write-Host $mdText
Write-Host ""
Write-Host "Attachments: $exeName, $exeName.sha256, latest.json" -ForegroundColor DarkGray
Write-Host ""
Write-Host "==== Discord post preview ====" -ForegroundColor Cyan
Write-Host $embed.title -ForegroundColor White
foreach ($fl in $fields) { Write-Host ""; Write-Host $fl.name -ForegroundColor Green; Write-Host $fl.value }
Write-Host "==============================" -ForegroundColor Cyan
if ($Preview) { Write-Host ""; Write-Host "Preview only - nothing was published or posted." -ForegroundColor Yellow; return }

# ---- GitHub release ----------------------------------------------------------
if (-not $SkipGithub) {
    # gh writes notes such as 'release not found' to stderr. Windows PowerShell 5.1 turns redirected stderr
    # into errors, which stop the script under 'Stop'; exit codes are checked instead in this block.
    $ErrorActionPreference = 'Continue'
    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) { throw "GitHub CLI (gh) is not installed. Install it (winget install --id GitHub.cli, then gh auth login) and rerun - or publish by hand on https://github.com/$Repo/releases/new (tag $tag, notes from $notesMd, attach $exeName, its .sha256 and latest.json from $deliver), then rerun with -SkipGithub for the Discord post." }
    & gh auth status *> $null
    if ($LASTEXITCODE -ne 0) { throw "gh is not signed in. Run 'gh auth login' first." }

    # Can this account publish there at all? (Releases need push access to the repo.)
    $who = (& gh api user --jq .login 2>$null | Out-String).Trim()
    $perm = (& gh api "repos/$Repo" --jq .permissions.push 2>$null | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "gh could not open github.com/$Repo (signed in as '$who'). Check the repo name and that it exists." }
    if ($perm -ne 'true') { throw "The GitHub account '$who' is not allowed to publish releases on $Repo. The owner must add '$who' as a collaborator (repo Settings > Collaborators), you accept the email invite, then run this again." }
    # A brand-new repo with no commits can't hold a release (the tag needs a commit to point at).
    & gh api "repos/$Repo/commits?per_page=1" *> $null
    $emptyRepo = ($LASTEXITCODE -ne 0)

    $exists = $false
    & gh release view $tag --repo $Repo *> $null
    if ($LASTEXITCODE -eq 0) { $exists = $true }

    $go = $Yes
    if (-not $go) {
        $prompt = if ($exists) { "Release $tag already exists on $Repo. Update its notes and re-upload the files? (y/n)" } else { "Create release $tag on $Repo and upload the EXE? (y/n)" }
        $go = (Read-Host $prompt) -match '^(y|yes)$'
    }
    if ($go) {
        if ($emptyRepo) {
            Write-Host "$Repo has no commits yet; adding a README so the release has a commit to point at." -ForegroundColor Yellow
            $readme = "# Syndicate`n`nDownloads for Syndicate. Get the latest EXE from [Releases](https://github.com/$Repo/releases/latest).`n`nAlready installed? Open Syndicate - it updates itself.`n"
            $b64 = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($readme))
            & gh api -X PUT "repos/$Repo/contents/README.md" -f "message=Add README" -f "content=$b64" | Out-Null
            if ($LASTEXITCODE) { throw "Could not add the first commit (README) to $Repo." }
        }
        # The release is made first as a draft with no files, then the files go up (the EXE is a few hundred MB,
        # so an upload can drop on a slow or busy connection; each file is tried up to 4 times), and only then
        # is the release made public. A run that failed halfway leaves a draft: running this again finishes it.
        if ($exists) {
            & gh release edit $tag --repo $Repo --title "Syndicate $tag" --notes-file $notesMd
            if ($LASTEXITCODE) { throw "Could not update the release notes." }
        } else {
            & gh release create $tag --repo $Repo --title "Syndicate $tag" --notes-file $notesMd --draft
            if ($LASTEXITCODE) { throw "Could not create the release (no files were uploaded yet)." }
        }
        foreach ($file in @($shaFile, $latestFile, $exe)) {
            $name = Split-Path -Leaf $file
            $sent = $false
            for ($try = 1; $try -le 4 -and -not $sent; $try++) {
                Write-Host "Uploading $name$(if ($try -gt 1) { " (try $try of 4)" })..." -ForegroundColor DarkGray
                & gh release upload $tag $file --repo $Repo --clobber
                if ($LASTEXITCODE -eq 0) { $sent = $true } elseif ($try -lt 4) { Start-Sleep -Seconds (10 * $try) }
            }
            if (-not $sent) { throw "Could not upload $name after 4 tries. Check your internet connection and run PUBLISH PUBLIC RELEASE again - it picks up the draft release $tag and only re-sends the files." }
        }
        & gh release edit $tag --repo $Repo --draft=false --latest
        if ($LASTEXITCODE) { throw "The files are uploaded, but the release could not be made public. Open https://github.com/$Repo/releases and press Publish release on the $tag draft." }
        Write-Host "GitHub release $tag is live: $releasePage" -ForegroundColor Green
    } else { Write-Host "Skipped the GitHub release." -ForegroundColor Yellow }
    $ErrorActionPreference = 'Stop'
}

# ---- Discord announcement ----------------------------------------------------
if (-not $SkipDiscord) {
    $hookDir = Join-Path $env:APPDATA 'SyndicateClassic-Release'
    $hookFile = Join-Path $hookDir 'discord-release-webhook.txt'
    # Also accepted next to the other per-PC Discord files (never committed either).
    $altHook = Join-Path $env:LOCALAPPDATA 'SyndicateClassic\UserData\discord-release-webhook.txt'
    if (!(Test-Path $hookFile) -and (Test-Path $altHook)) { $hookFile = $altHook }
    if (!(Test-Path $hookFile)) {
        Write-Host ""
        Write-Host "No Discord webhook on this PC yet. To post release announcements:" -ForegroundColor Yellow
        Write-Host "  1. In Discord: Channel settings > Integrations > Webhooks > New Webhook > Copy URL."
        Write-Host "  2. Save it to: $hookFile"
        Write-Host "     (This folder is on your PC only and is never committed.)"
        Write-Host "Then rerun with -SkipGithub to post just the announcement." -ForegroundColor Yellow
        return
    }
    $hook = (Get-Content $hookFile -Raw).Trim()
    if ($hook -notmatch '^https://(discord|discordapp)\.com/api/webhooks/\d+/[\w-]+$') { throw "$hookFile does not contain a Discord webhook URL." }

    $go = $Yes
    if (-not $go) { $go = (Read-Host "Post the announcement to Discord now? (y/n)") -match '^(y|yes)$' }
    if ($go) {
        $payload = @{
            username = "Project Tilted"
            content = if ($Mention) { "$Mention New update!" } else { "" }
            embeds = @($embed)
            allowed_mentions = @{ parse = @("everyone", "roles") }
        } | ConvertTo-Json -Depth 8
        $tmp = Join-Path $env:TEMP ("syndicate-post-" + [guid]::NewGuid() + ".json")
        [IO.File]::WriteAllText($tmp, $payload, (New-Object Text.UTF8Encoding $false))
        try {
            $reply = Join-Path $env:TEMP ("syndicate-reply-" + [guid]::NewGuid() + ".txt")
            $code = & curl.exe -sS -o $reply -w "%{http_code}" -X POST "$hook`?wait=true" -F "payload_json=<$tmp;type=application/json"
            $body = Get-Content $reply -Raw -ErrorAction SilentlyContinue
            Remove-Item $reply -ErrorAction SilentlyContinue
            if ($LASTEXITCODE -ne 0 -or $code -notmatch '^2') { throw "Discord refused the post (HTTP $code): $body" }
            Write-Host "Posted the Syndicate $tag announcement to Discord." -ForegroundColor Green
        } finally { Remove-Item $tmp -ErrorAction SilentlyContinue }
    } else { Write-Host "Skipped the Discord post." -ForegroundColor Yellow }
}
