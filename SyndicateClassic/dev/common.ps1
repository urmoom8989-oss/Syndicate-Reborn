# Shared helpers for UPDATE FROM GIT and MAKE DEV KIT (dot-sourced, Windows PowerShell 5.1).

$SyndicateDevState = Join-Path $env:LOCALAPPDATA 'SyndicateClassic\dev'
New-Item -ItemType Directory -Path $SyndicateDevState -Force | Out-Null

function Step($text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Cyan }

# The backend folder is "Syndicate-backend"; installs the launcher has not renamed yet still have "Helix-backend-main".
function Get-BackendFolderName($root) {
  if ((Test-Path (Join-Path $root 'Syndicate-backend')) -or !(Test-Path (Join-Path $root 'Helix-backend-main'))) { return 'Syndicate-backend' }
  return 'Helix-backend-main'
}
function Test-SyndicateRoot($path) {
  return ($path -and (Test-Path (Join-Path $path ((Get-BackendFolderName $path) + '\src\index.js'))) -and (Test-Path (Join-Path $path 'Launcher')))
}

# The "Project Tilted" folder: remembered after the first time, otherwise found or picked once.
function Find-SyndicateRoot([string]$Given) {
  $saved = Join-Path $SyndicateDevState 'syndicate-root.txt'
  $candidates = @($Given)
  if (Test-Path $saved) { $candidates += (Get-Content -LiteralPath $saved -Raw).Trim() }
  $candidates += @(
    (Join-Path $env:USERPROFILE 'Desktop\Syndicate'),
    (Join-Path $env:USERPROFILE 'Documents\Syndicate'),
    (Join-Path $env:USERPROFILE 'Downloads\Syndicate'),
    (Join-Path $env:LOCALAPPDATA 'Project Tilted')
  )
  $found = $null
  foreach ($c in $candidates) { if (Test-SyndicateRoot $c) { $found = (Resolve-Path $c).Path; break } }
  if (!$found) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select your "Project Tilted" folder (the one with Syndicate-backend, Launcher and Source). This is asked only once.'
    if ($dialog.ShowDialog() -ne 'OK') { throw 'No Syndicate folder selected.' }
    if (!(Test-SyndicateRoot $dialog.SelectedPath)) { throw "That folder has no Syndicate-backend\src\index.js or Launcher folder. Pick the Syndicate folder from the dev kit." }
    $found = $dialog.SelectedPath
  }
  [IO.File]::WriteAllText($saved, $found)
  return $found
}

# One hash for a whole folder (file names + contents), to skip builds when nothing changed.
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
  $file = Join-Path $SyndicateDevState "$name.hash"
  if (Test-Path $file) { return (Get-Content -LiteralPath $file -Raw).Trim() }
  return ''
}
function Set-DevState([string]$name, [string]$value) { [IO.File]::WriteAllText((Join-Path $SyndicateDevState "$name.hash"), $value) }
