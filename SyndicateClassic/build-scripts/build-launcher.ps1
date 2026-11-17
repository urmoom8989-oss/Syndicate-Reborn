# Builds the fixed Syndicate launcher and installs SyndicateLauncher.dll into Syndicate\Launcher.
# Requires the .NET 8 SDK (you already have one in your user folder). Close Syndicate first.
# -Root: the "Project Tilted" folder (UPDATE FROM GIT passes it so nobody has to pick it).
# -Public: build for the public release: no Developer Tools (no TILTED_DEVKIT symbol). Without it the
#          build is a Dev Kit build, which always shows Settings > Match data > Developer Tools.
# -OutFile: copy the built SyndicateLauncher.dll there instead of installing it (used by the release script).
# -Version: the version the launcher shows ("Launcher v0.4.10", Settings, error reports). The release script
#           passes the release's version; without it the newest version in public-release\release-notes.json
#           is used, so the launcher never shows an older number than the release it ships in.
param([string]$Root = "", [switch]$Public, [string]$OutFile = "", [string]$Version = "")
$ErrorActionPreference = 'Stop'
$fixes = $PSScriptRoot
# Find the Syndicate install (this folder can live anywhere, e.g. the Desktop).
function Find-SyndicateRoot {
  if ($Root -and ((Test-Path (Join-Path $Root 'Syndicate-backend\src\index.js')) -or (Test-Path (Join-Path $Root 'Helix-backend-main\src\index.js')))) { return (Resolve-Path $Root).Path }
  $candidates = @(
    (Join-Path $PSScriptRoot '..\..'),
    (Join-Path $env:USERPROFILE 'Desktop\fortnite syndicate\Syndicate'),
    (Join-Path $env:LOCALAPPDATA 'Project Tilted')
  )
  foreach ($c in $candidates) {
    if ($c -and ((Test-Path (Join-Path $c 'Syndicate-backend\src\index.js')) -or (Test-Path (Join-Path $c 'Helix-backend-main\src\index.js'))) -and (Test-Path (Join-Path $c 'Source'))) { return (Resolve-Path $c).Path }
  }
  Add-Type -AssemblyName System.Windows.Forms
  $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
  $dialog.Description = 'Select your "Project Tilted" folder (the one containing Syndicate-backend, Launcher and Source)'
  if ($dialog.ShowDialog() -ne 'OK') { throw 'No Syndicate folder selected.' }
  return $dialog.SelectedPath
}
$root  = Find-SyndicateRoot
Write-Host "Using Syndicate folder: $root"
$src   = Join-Path $root 'Source\PublicStandalone040\launcher-source'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$work  = Join-Path $env:TEMP "SyndicateLauncherBuild-$stamp"
$target= Join-Path $root 'Launcher'

function Step($text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Cyan }

if (!$OutFile -and (Get-Process SyndicateLauncher -ErrorAction SilentlyContinue)) { throw 'Close Syndicate Launcher (and Fortnite) first, then run this again.' }
if (!(Get-Command dotnet -ErrorAction SilentlyContinue)) { throw 'The .NET 8 SDK is required: https://dotnet.microsoft.com/download/dotnet/8.0' }

Step 'Copying launcher source'
if (!(Test-Path -LiteralPath $src)) { throw "Launcher source not found: $src (pick the Syndicate folder that has Source\PublicStandalone040\launcher-source)." }
$copyLog = robocopy $src $work /MIR /XD bin obj publish /R:2 /W:1 /NFL /NDL /NJH /NP 2>&1
$code = $LASTEXITCODE
if ($code -ge 8) {
  # Show robocopy's own reason, then copy without it (bin, obj and publish left out the same way).
  Write-Host "robocopy could not copy the launcher source (code $code):" -ForegroundColor Yellow
  $copyLog | Where-Object { $_ -match 'ERROR|error|denied|cannot|not found' } | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
  Write-Host "Copying it another way..." -ForegroundColor Yellow
  if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
  New-Item -ItemType Directory -Path $work -Force | Out-Null
  $skip = @('bin','obj','publish')
  $base = (Resolve-Path -LiteralPath $src).Path.TrimEnd('\')
  Get-ChildItem -LiteralPath $base -Recurse -Force -File | Where-Object {
    $rel = $_.FullName.Substring($base.Length + 1); -not ($rel.Split('\') | Where-Object { $skip -contains $_ })
  } | ForEach-Object {
    $rel = $_.FullName.Substring($base.Length + 1); $to = Join-Path $work $rel
    $dir = Split-Path $to -Parent; if (!(Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    Copy-Item -LiteralPath $_.FullName -Destination $to -Force
  }
  if (!(Get-ChildItem -LiteralPath $work -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1)) { throw "Copy failed (robocopy code $code, and the fallback copied nothing) from $src to $work." }
  Write-Host "Launcher source copied." -ForegroundColor Green
}
if (Test-Path (Join-Path $fixes 'source-files')) { Copy-Item -Path (Join-Path $fixes 'source-files\*') -Destination (Join-Path $work 'ProjectTilted') -Force }
# Stamp the launcher's version (it used to stay at 0.3.40 whatever the release was called).
if (!$Version) {
  $notesFile = Join-Path (Split-Path $fixes -Parent) 'public-release\release-notes.json'
  if (Test-Path $notesFile) { try { $Version = "$((Get-Content -LiteralPath $notesFile -Raw | ConvertFrom-Json)[0].version)".Trim() } catch { $Version = "" } }
}
if ($Version -and $Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must look like 0.4.10 (got '$Version')." }
$assemblyInfo = Join-Path $work 'Properties\AssemblyInfo.cs'
if ($Version -and (Test-Path $assemblyInfo)) {
  $info = [IO.File]::ReadAllText($assemblyInfo)
  $info = [regex]::Replace($info, 'AssemblyFileVersion\("[^"]*"\)', "AssemblyFileVersion(`"$Version`")")
  $info = [regex]::Replace($info, 'AssemblyInformationalVersion\("[^"]*"\)', "AssemblyInformationalVersion(`"$Version`")")
  $info = [regex]::Replace($info, 'AssemblyVersion\("[^"]*"\)', "AssemblyVersion(`"$Version.0`")")
  [IO.File]::WriteAllText($assemblyInfo, $info)
  Write-Host "Launcher version: $Version"
}
# The repo's program icon replaces the one in the PC's source copy, so the launcher and the EXE match.
$repoIcon = Join-Path (Split-Path $fixes -Parent) 'launcher\launcher-source\SyndicateClassic.ico'
if (!(Test-Path $repoIcon)) { $repoIcon = Join-Path $fixes 'SyndicateClassic.ico' }
if (Test-Path $repoIcon) { Copy-Item -LiteralPath $repoIcon -Destination (Join-Path $work 'SyndicateClassic.ico') -Force }

# The source pins one exact .NET SDK version; allow any .NET 8 SDK so a new PC can build it too.
$globalJson = Join-Path $work 'global.json'
if (Test-Path $globalJson) { [IO.File]::WriteAllText($globalJson, ([IO.File]::ReadAllText($globalJson) -replace '"rollForward"\s*:\s*"disable"', '"rollForward":"latestFeature"')) }
# Extra season art (season-art\season6.png ...) is embedded as SyndicateClassic.SeasonCard6 and so on.
# Packages carry their own season-art folder; a plain repo checkout uses the launcher source's copy.
$artDir = Join-Path $fixes 'season-art'
if (!(Test-Path $artDir)) { $artDir = Join-Path (Split-Path $fixes -Parent) 'launcher\launcher-source\season-art' }
if (Test-Path $artDir) {
  $projFile = Join-Path $work 'SyndicateLauncher.original.csproj'
  $proj = [IO.File]::ReadAllText($projFile)
  New-Item -ItemType Directory -Path (Join-Path $work 'Assets') -Force | Out-Null
  foreach ($art in Get-ChildItem $artDir -Filter 'season*.png') {
    if ($art.BaseName -notmatch '^season(\d+)$') { continue }
    $logical = 'SyndicateClassic.SeasonCard' + $Matches[1]
    $existing = [regex]::Match($proj, '<EmbeddedResource Include="([^"]+)" LogicalName="' + [regex]::Escape($logical) + '" />')
    if ($existing.Success) {
      # The project already embeds a picture under this name (e.g. Season 4's old reference art): replace that file.
      Copy-Item -LiteralPath $art.FullName -Destination (Join-Path $work ($existing.Groups[1].Value -replace '/', '\')) -Force
    } else {
      Copy-Item -LiteralPath $art.FullName -Destination (Join-Path $work ('Assets\' + $art.Name)) -Force
      $proj = $proj.Replace('</Project>', '<ItemGroup><EmbeddedResource Include="Assets/' + $art.Name + '" LogicalName="' + $logical + '" /></ItemGroup></Project>')
    }
  }
  # Chapter 2 Season 3-8 key art (jpg) — logical names SyndicateClassic.Chapter2Season3..8.
  New-Item -ItemType Directory -Path (Join-Path $work 'season-art') -Force | Out-Null
  foreach ($art in Get-ChildItem $artDir -Filter 'chapter2-season*.jpg') {
    if ($art.BaseName -notmatch '^chapter2-season(\d+)$') { continue }
    $logical = 'SyndicateClassic.Chapter2Season' + $Matches[1]
    Copy-Item -LiteralPath $art.FullName -Destination (Join-Path $work ('season-art\' + $art.Name)) -Force
    if (-not $proj.Contains('LogicalName="' + $logical + '"')) {
      $proj = $proj.Replace('</Project>', '<ItemGroup><EmbeddedResource Include="season-art/' + $art.Name + '" LogicalName="' + $logical + '" /></ItemGroup></Project>')
    }
  }
  [IO.File]::WriteAllText($projFile, $proj)
}

# Launcher logo: logo.png next to this script is embedded as SyndicateClassic.Logo (top-left brand).
$logoFile = Join-Path $fixes 'logo.png'
if (Test-Path $logoFile) {
  $projFile = Join-Path $work 'SyndicateLauncher.original.csproj'
  $proj = [IO.File]::ReadAllText($projFile)
  New-Item -ItemType Directory -Path (Join-Path $work 'Assets') -Force | Out-Null
  Copy-Item -LiteralPath $logoFile -Destination (Join-Path $work 'Assets\syndicate-logo.png') -Force
  if (-not $proj.Contains('LogicalName="SyndicateClassic.Logo"')) {
    $proj = $proj.Replace('</Project>', '<ItemGroup><EmbeddedResource Include="Assets/syndicate-logo.png" LogicalName="SyndicateClassic.Logo" /></ItemGroup></Project>')
  }
  [IO.File]::WriteAllText($projFile, $proj)
}

# Launcher music: every music\*.tadp (IMA ADPCM made from an mp3 with tools/make-music.py, see MenuMusicPlayer.cs) is
# embedded as SyndicateClassic.Music.<file name> and shows in the launcher's player card, in file name order
# ("01 - Menu Theme.tadp" plays first, as "Menu Theme"). Played through winmm: works without Windows Media Player.
$musicDir = Join-Path $fixes 'music'
if (!(Test-Path $musicDir)) { $musicDir = Join-Path (Split-Path $fixes -Parent) 'launcher\launcher-source\music' }
$tracks = @(if (Test-Path $musicDir) { Get-ChildItem -LiteralPath $musicDir -Filter *.tadp -File | Sort-Object Name })
if ($tracks.Count -gt 0) {
  $projFile = Join-Path $work 'SyndicateLauncher.original.csproj'
  $proj = [IO.File]::ReadAllText($projFile)
  $proj = [regex]::Replace($proj, '<ItemGroup><EmbeddedResource Include="Music/[^"]*" LogicalName="SyndicateClassic\.Music\.[^"]*" /></ItemGroup>', '')
  New-Item -ItemType Directory -Path (Join-Path $work 'Music') -Force | Out-Null
  $items = ''
  for ($i = 0; $i -lt $tracks.Count; $i++) {
    $safe = 'track' + $i.ToString('00') + '.tadp'
    Copy-Item -LiteralPath $tracks[$i].FullName -Destination (Join-Path $work ('Music\' + $safe)) -Force
    $logical = [Security.SecurityElement]::Escape('SyndicateClassic.Music.' + $tracks[$i].Name)
    $items += '<ItemGroup><EmbeddedResource Include="Music/' + $safe + '" LogicalName="' + $logical + '" /></ItemGroup>'
  }
  $proj = $proj.Replace('</Project>', $items + '</Project>')
  [IO.File]::WriteAllText($projFile, $proj)
  Write-Host "Launcher music: $($tracks.Count) track(s) embedded."
} else {
  Write-Host "Launcher music: no music\*.tadp found; the launcher starts without music." -ForegroundColor Yellow
}

# Pack and exclusive art: pack-art\<pack id>.png (for example pack-art\x-galaxy.png)
# is embedded as SyndicateClassic.Pack.<pack id> and replaces any built-in picture.
$packDir = Join-Path $fixes 'pack-art'
if (Test-Path $packDir) {
  $projFile = Join-Path $work 'SyndicateLauncher.original.csproj'
  $proj = [IO.File]::ReadAllText($projFile)
  New-Item -ItemType Directory -Path (Join-Path $work 'Assets') -Force | Out-Null
  # .webp (and .jpg) pictures are converted to PNG here, so any of those formats work.
  Add-Type -AssemblyName PresentationCore
  $converted = Join-Path $work 'pack-art-png'
  New-Item -ItemType Directory -Path $converted -Force | Out-Null
  # A .png always wins over a .webp/.jpg with the same name.
  foreach ($pic in Get-ChildItem $packDir -File | Where-Object { $_.Extension -in '.webp', '.jpg', '.jpeg' -and -not (Test-Path (Join-Path $packDir ($_.BaseName + '.png'))) }) {
    try {
      $stream = [IO.File]::OpenRead($pic.FullName)
      try {
        $decoder = [System.Windows.Media.Imaging.BitmapDecoder]::Create($stream, [System.Windows.Media.Imaging.BitmapCreateOptions]::None, [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
        $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
        $encoder.Frames.Add($decoder.Frames[0])
        $out = [IO.File]::Create((Join-Path $converted ($pic.BaseName + '.png')))
        try { $encoder.Save($out) } finally { $out.Dispose() }
      } finally { $stream.Dispose() }
    } catch { Write-Host "Could not read $($pic.Name): $($_.Exception.Message). For .webp, install 'WebP Image Extensions' from the Microsoft Store, or save it as .png." -ForegroundColor Yellow }
  }
  $pictures = @(Get-ChildItem $converted -Filter '*.png') + @(Get-ChildItem $packDir -Filter '*.png' | Where-Object { -not (Test-Path (Join-Path $converted $_.Name)) })
  foreach ($art in $pictures) {
    $logical = 'SyndicateClassic.Pack.' + $art.BaseName
    $existing = [regex]::Match($proj, '<EmbeddedResource Include="([^"]+)" LogicalName="' + [regex]::Escape($logical) + '" />')
    if ($existing.Success) {
      Copy-Item -LiteralPath $art.FullName -Destination (Join-Path $work ($existing.Groups[1].Value -replace '/', '\')) -Force
    } else {
      $name = 'pack-' + $art.Name
      Copy-Item -LiteralPath $art.FullName -Destination (Join-Path $work ('Assets\' + $name)) -Force
      $proj = $proj.Replace('</Project>', '<ItemGroup><EmbeddedResource Include="Assets/' + $name + '" LogicalName="' + $logical + '" /></ItemGroup></Project>')
    }
    Write-Host "Pack art: $($art.Name)"
  }
  [IO.File]::WriteAllText($projFile, $proj)
}

# Profile icons: avatars\og01.png ... are embedded as SyndicateClassic.Avatar.og01 and so on.
$avatarDir = Join-Path $fixes 'avatars'
if (Test-Path $avatarDir) {
  $projFile = Join-Path $work 'SyndicateLauncher.original.csproj'
  $proj = [IO.File]::ReadAllText($projFile)
  New-Item -ItemType Directory -Path (Join-Path $work 'Assets\avatars') -Force | Out-Null
  foreach ($icon in Get-ChildItem $avatarDir -Filter '*.png') {
    $logical = 'SyndicateClassic.Avatar.' + $icon.BaseName
    Copy-Item -LiteralPath $icon.FullName -Destination (Join-Path $work ('Assets\avatars\' + $icon.Name)) -Force
    if (-not $proj.Contains('LogicalName="' + $logical + '"')) {
      $proj = $proj.Replace('</Project>', '<ItemGroup><EmbeddedResource Include="Assets/avatars/' + $icon.Name + '" LogicalName="' + $logical + '" /></ItemGroup></Project>')
    }
  }
  [IO.File]::WriteAllText($projFile, $proj)
}

# Controller button icons: controller-icons\a.png ... are embedded as SyndicateClassic.Pad.a and so on
# (button hints in the launcher, ControllerNav.cs).
$padDir = Join-Path $fixes 'controller-icons'
if (Test-Path $padDir) {
  $projFile = Join-Path $work 'SyndicateLauncher.original.csproj'
  $proj = [IO.File]::ReadAllText($projFile)
  New-Item -ItemType Directory -Path (Join-Path $work 'Assets\controller-icons') -Force | Out-Null
  foreach ($icon in Get-ChildItem $padDir -Filter '*.png') {
    $logical = 'SyndicateClassic.Pad.' + $icon.BaseName
    Copy-Item -LiteralPath $icon.FullName -Destination (Join-Path $work ('Assets\controller-icons\' + $icon.Name)) -Force
    if (-not $proj.Contains('LogicalName="' + $logical + '"')) {
      $proj = $proj.Replace('</Project>', '<ItemGroup><EmbeddedResource Include="Assets/controller-icons/' + $icon.Name + '" LogicalName="' + $logical + '" /></ItemGroup></Project>')
    }
  }
  [IO.File]::WriteAllText($projFile, $proj)
}

# Dev Kit or public: a compile-time symbol, so public DLLs do not contain the developer code at all.
$projFile = Join-Path $work 'SyndicateLauncher.original.csproj'
if (-not $Public) {
  $proj = [IO.File]::ReadAllText($projFile)
  $proj = $proj.Replace('</Project>', '<PropertyGroup><DefineConstants>$(DefineConstants);TILTED_DEVKIT</DefineConstants></PropertyGroup></Project>')
  [IO.File]::WriteAllText($projFile, $proj)
}
Write-Host ('Build kind: ' + $(if ($Public) { 'public release (no Developer Tools)' } else { 'Dev Kit (Developer Tools on)' }))
# Controller support (PS4/PS5 pads mirrored as Xbox pads, same libraries as Project Springs).
$proj = [IO.File]::ReadAllText($projFile)
if (-not $proj.Contains('Nefarius.ViGEm.Client')) {
  $proj = $proj.Replace('</Project>', '<ItemGroup><PackageReference Include="HidSharp" Version="2.1.0" /><PackageReference Include="Nefarius.ViGEm.Client" Version="1.21.256" /></ItemGroup></Project>')
  [IO.File]::WriteAllText($projFile, $proj)
}

# Public builds only: obfuscate the launcher's code so the released EXE is harder to read and tamper
# with. Private methods/fields are renamed and string literals encrypted; property/event names, the
# App entry point, the MainWindow XAML fields and the JSON/anonymous types are kept so nothing breaks.
# This raises the effort to reverse engineer but cannot prevent it (the key to run must live in the EXE).
$obfArgs = @()
if ($Public) {
  Step 'Preparing obfuscation (public build)'
  $toolDir = Join-Path $env:LOCALAPPDATA 'SyndicateClassic-Tools\obfuscar'
  $obf = Get-ChildItem $toolDir -Recurse -Filter 'obfuscar.console.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $obf) {
    New-Item -ItemType Directory -Force -Path $toolDir | Out-Null
    Write-Host 'Installing Obfuscar...' -ForegroundColor Cyan
    dotnet tool install --tool-path $toolDir Obfuscar.GlobalTool | Out-Host
    $obf = Get-ChildItem $toolDir -Recurse -Filter 'obfuscar.console.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $obf) { throw 'Obfuscar could not be installed (needed for the public build). Nothing was handed out.' }
  }
  # Run Obfuscar's .NET 8 build on a .NET 8 runtime. The obfuscar.console.exe shim picks the newest
  # build in the package (.NET 10), and Windows stops .NET 10 programs here ("doesn't fully support CET").
  $obfDll = Get-ChildItem (Join-Path $toolDir '.store') -Recurse -Filter 'GlobalTools.dll' -ErrorAction SilentlyContinue |
            Where-Object { $_.Directory.Parent.Name -eq 'net8.0' } | Select-Object -First 1
  if (-not $obfDll) { throw "Obfuscar's .NET 8 files were not found in $toolDir. Delete that folder and run this again." }
  $hosts = @()
  if ($env:DOTNET_ROOT) { $hosts += (Join-Path $env:DOTNET_ROOT 'dotnet.exe') }
  $hosts += (Join-Path $env:USERPROFILE '.dotnet\dotnet.exe'), (Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'), (Join-Path $env:ProgramFiles 'dotnet\dotnet.exe')
  $onPath = Get-Command dotnet -ErrorAction SilentlyContinue
  if ($onPath) { $hosts += $onPath.Source }
  $obfHost = $hosts | Select-Object -Unique | Where-Object {
    (Test-Path -LiteralPath $_) -and (Get-ChildItem (Join-Path (Split-Path $_) 'shared\Microsoft.NETCore.App') -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '8.0.*' })
  } | Select-Object -First 1
  if (-not $obfHost) { throw 'No .NET 8 runtime found to run Obfuscar. Install the .NET 8 SDK: https://dotnet.microsoft.com/download/dotnet/8.0' }
  Write-Host "Obfuscar: $($obfDll.FullName) on $obfHost"
  $obfArgs = @('-p:SyndicateObfuscate=true', "-p:SyndicateObfuscarHost=$obfHost", "-p:SyndicateObfuscarDll=$($obfDll.FullName)")
  # Inject a target that runs Obfuscar on the compiled DLL in obj\ (after CoreCompile, before ReadyToRun
  # and publish), then copies the obfuscated DLL back so that is what gets packed.
  $proj = [IO.File]::ReadAllText($projFile)
  $obfTarget = @'
<Target Name="SyndicateObfuscate" AfterTargets="CoreCompile" Condition="'$(SyndicateObfuscate)' == 'true' And !$(MSBuildProjectName.EndsWith('_wpftmp'))">
  <PropertyGroup>
    <_ObfIn>$(MSBuildProjectDirectory)\$(IntermediateOutputPath)</_ObfIn>
    <_ObfOut>$(MSBuildProjectDirectory)\$(IntermediateOutputPath)obfuscated\</_ObfOut>
    <_ObfConfig>$(MSBuildProjectDirectory)\$(IntermediateOutputPath)obfuscar.generated.xml</_ObfConfig>
  </PropertyGroup>
  <ItemGroup>
    <_ObfLine Include="&lt;Obfuscator&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;InPath&quot; value=&quot;$(_ObfIn)&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;OutPath&quot; value=&quot;$(_ObfOut)&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;KeepPublicApi&quot; value=&quot;true&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;HidePrivateApi&quot; value=&quot;true&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;RenameProperties&quot; value=&quot;false&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;RenameEvents&quot; value=&quot;false&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;ReuseNames&quot; value=&quot;false&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;SkipGenerated&quot; value=&quot;true&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;HideStrings&quot; value=&quot;true&quot; /&gt;" />
    <_ObfLine Include="&lt;Var name=&quot;RegenerateDebugInfo&quot; value=&quot;false&quot; /&gt;" />
    <_ObfLine Include="@(ReferencePath->'&lt;AssemblySearchPath path=&quot;%(RootDir)%(Directory)&quot; /&gt;')" />
    <_ObfLine Include="&lt;Module file=&quot;$(_ObfIn)SyndicateLauncher.dll&quot;&gt;&lt;SkipNamespace name=&quot;SyndicateClassic.Models&quot; /&gt;&lt;SkipType name=&quot;SyndicateClassic.App&quot; /&gt;&lt;SkipType rx=&quot;AnonymousType&quot; skipMethods=&quot;true&quot; skipFields=&quot;true&quot; skipProperties=&quot;true&quot; /&gt;&lt;SkipType rx=&quot;(^|[./+])(ServerState|Occupancy|PlaylistRequest|Config|Settings|Entry|SnapshotFile|Owned|Data|FriendAdvert)$&quot; skipMethods=&quot;true&quot; skipFields=&quot;true&quot; skipProperties=&quot;true&quot; /&gt;&lt;SkipType name=&quot;SyndicateClassic.MainWindow&quot; skipFields=&quot;true&quot; /&gt;&lt;/Module&gt;" />
    <_ObfLine Include="&lt;/Obfuscator&gt;" />
  </ItemGroup>
  <WriteLinesToFile File="$(_ObfConfig)" Lines="@(_ObfLine)" Overwrite="true" />
  <Exec Command="&quot;$(SyndicateObfuscarHost)&quot; exec &quot;$(SyndicateObfuscarDll)&quot; &quot;$(_ObfConfig)&quot;" />
  <Copy SourceFiles="$(_ObfOut)SyndicateLauncher.dll" DestinationFolder="$(_ObfIn)" />
  <Message Importance="high" Text="Obfuscated SyndicateLauncher.dll" />
</Target>
'@
  $proj = $proj.Replace('</Project>', $obfTarget + "`n</Project>")
  [IO.File]::WriteAllText($projFile, $proj)
}

Step 'Building'
Push-Location $work
try {
  # ReadyToRun: the launcher's code is compiled ahead of time, so it opens without waiting for the JIT.
  dotnet publish SyndicateLauncher.original.csproj -c Release -r win-x64 --self-contained true -p:PublishReadyToRun=true @obfArgs -o out
  if ($LASTEXITCODE) { throw "Build failed - see the errors above. Nothing was installed. Build folder: $work" }
} finally { Pop-Location }

# Public build: prove the obfuscated launcher still opens before it is handed out. A broken obfuscation
# writes FAIL (or never renders), and the build stops here so a broken EXE is never shipped.
if ($Public) {
  Step 'Self-testing the obfuscated public launcher'
  $selfExe = Join-Path $work 'out\SyndicateLauncher.exe'
  if (!(Test-Path $selfExe)) { throw 'The public build produced no SyndicateLauncher.exe to self-test. Nothing was handed out.' }
  $selfTest = Join-Path $work 'selftest.txt'
  Remove-Item -LiteralPath $selfTest -ErrorAction SilentlyContinue
  $proc = Start-Process $selfExe -ArgumentList '--tilted-selftest', "`"$selfTest`"" -PassThru
  if (-not $proc.WaitForExit(60000)) { try { $proc.Kill() } catch {}; throw 'Self-test: the obfuscated launcher did not open within 60 s. It was NOT handed out.' }
  $result = if (Test-Path $selfTest) { (Get-Content -LiteralPath $selfTest -Raw).Trim() } else { 'FAIL no result' }
  Remove-Item -LiteralPath $selfTest -ErrorAction SilentlyContinue
  if (-not $result.StartsWith('OK')) { throw "Self-test failed on the obfuscated build - it was NOT handed out: $result" }
  Write-Host "Self-test passed: $result" -ForegroundColor Green
}

if ($OutFile) {
  Copy-Item -LiteralPath (Join-Path $work 'out\SyndicateLauncher.dll') -Destination $OutFile -Force
  Write-Host "Built $OutFile"
  return
}

Step 'Installing'
$backup = Join-Path $root "Runtime\Backups\launcher-before-$stamp"
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach ($name in 'SyndicateLauncher.dll', 'SyndicateLauncher.pdb', 'InstallUpdate.ps1') {
  $existing = Join-Path $target $name
  if (Test-Path -LiteralPath $existing) { Copy-Item -LiteralPath $existing -Destination $backup -Force }
}
Copy-Item -LiteralPath (Join-Path $work 'out\SyndicateLauncher.dll') -Destination $target -Force
if (Test-Path (Join-Path $work 'out\SyndicateLauncher.pdb')) { Copy-Item -LiteralPath (Join-Path $work 'out\SyndicateLauncher.pdb') -Destination $target -Force }
# Controller libraries (loaded by App.cs from this folder).
foreach ($lib in 'HidSharp.dll', 'Nefarius.ViGEm.Client.dll') {
  if (Test-Path (Join-Path $work "out\$lib")) { Copy-Item -LiteralPath (Join-Path $work "out\$lib") -Destination $target -Force }
}
Copy-Item -LiteralPath (Join-Path $fixes 'InstallUpdate.ps1') -Destination $target -Force
# Xbox controller sprint by default (pad-assist\TiltedPadAssist.dll, C++): the launcher loads it into
# Fortnite for Pre-season to Season 4. Needs Visual Studio's C++ tools; the launcher works without it.
$padSrc = Join-Path $fixes 'pad-assist'
if (Test-Path (Join-Path $padSrc 'SyndicatePadAssist.vcxproj')) {
  Step 'Building the Xbox sprint helper (TiltedPadAssist.dll)'
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  $msbuild = $null
  if (Test-Path $vswhere) { $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1 }
  if ($msbuild) {
    $padWork = Join-Path $work 'pad-assist'
    Copy-Item -LiteralPath $padSrc -Destination $padWork -Recurse -Force
    & $msbuild (Join-Path $padWork 'SyndicatePadAssist.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal
    if ($LASTEXITCODE) { Write-Host '  TiltedPadAssist.dll did not build (see above). Sprint by default on Xbox controllers stays off.' -ForegroundColor Yellow }
    else {
      try { Copy-Item -LiteralPath (Join-Path $padWork 'bin\TiltedPadAssist.dll') -Destination $target -Force; Write-Host '  TiltedPadAssist.dll installed.' }
      catch { Write-Host "  Could not replace TiltedPadAssist.dll (close Fortnite, then run again): $($_.Exception.Message)" -ForegroundColor Yellow }
    }
  } else { Write-Host '  Visual Studio C++ tools not found: skipped. Sprint by default on Xbox controllers stays off.' -ForegroundColor Yellow }
}

Write-Host ''
Write-Host 'Done. Open Launcher\SyndicateLauncher.exe.' -ForegroundColor Green
Write-Host "Previous launcher files saved in: $backup"
