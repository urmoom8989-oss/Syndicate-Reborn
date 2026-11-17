param([switch]$NoLaunch)
$ErrorActionPreference='Stop'
$log=Join-Path $env:LOCALAPPDATA 'SyndicateClassic\updater.log'
function Write-UpdateLog([string]$message){New-Item -ItemType Directory -Path (Split-Path $log) -Force | Out-Null;Add-Content -LiteralPath $log -Value ((Get-Date -Format o)+' '+$message)}
$changed=New-Object 'System.Collections.Generic.List[string]'
$target=$null
$backup=Join-Path $PSScriptRoot 'backup'
try {
 $config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'install.json') -Raw | ConvertFrom-Json
 $target=[IO.Path]::GetFullPath($config.target).TrimEnd('\')+'\'
 $stage=[IO.Path]::GetFullPath($config.stage).TrimEnd('\')+'\'
 if($stage -ne ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'stage')).TrimEnd('\')+'\')){throw 'Invalid staging folder'}
 if($target -eq $stage -or $target -eq [IO.Path]::GetPathRoot($target)){throw 'Invalid installation folder'}

 # Validate the complete component payload before changing any installed files.
 $manifestPath=Join-Path $stage 'components\manifest.json'
 if(!(Test-Path -LiteralPath $manifestPath)){throw 'Release is missing its component manifest.'}
 $componentRoot=[IO.Path]::GetFullPath((Join-Path $stage 'components')).TrimEnd('\')+'\'
 $entries=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
 if(!$entries.Count){throw 'Release component manifest is empty.'}
 foreach($entry in $entries){
  $source=[IO.Path]::GetFullPath((Join-Path $componentRoot $entry.Path))
  if(!$source.StartsWith($componentRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid component payload path.'}
  if(!(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $entry.Sha256){throw ('Component payload verification failed before installation: '+$entry.Path)}
 }
 if($config.repair -and !(Test-Path -LiteralPath (Join-Path $stage 'repair-payload.json'))){throw 'Release is missing full repair metadata.'}
 Write-UpdateLog ('Installing into '+$target)
 if($config.pid -gt 0){
  $old=Get-Process -Id $config.pid -ErrorAction SilentlyContinue
  if($old -and $old.StartTime.ToUniversalTime().Ticks -eq [long]$config.started){if(!$old.WaitForExit(60000)){throw 'Close the existing Syndicate launcher and retry.'}}
 }
 # Syndicate fix: a launcher still running from the target folder keeps clrjit.dll and
 # other runtime files locked. Copying then fails halfway and the rollback can delete
 # files, leaving an "incomplete payload". Close it first, before changing anything.
 $targetExe=Join-Path $target 'SyndicateLauncher.exe'
 $running=@(Get-Process SyndicateLauncher -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -and ([IO.Path]::GetFullPath($_.Path) -ieq [IO.Path]::GetFullPath($targetExe)) } catch { $false } })
 foreach($process in $running){
  Write-UpdateLog ('Closing running launcher (pid '+$process.Id+') before update')
  try { $null=$process.CloseMainWindow() } catch {}
  if(!$process.WaitForExit(20000)){ throw 'Syndicate Launcher is still open from the folder being updated. Close it (check the system tray), then run the update again. Nothing was changed.' }
 }
 Start-Sleep -Milliseconds 750
 $files=@(Get-ChildItem -LiteralPath $stage -File -Recurse)
 foreach($file in $files){
  $relative=$file.FullName.Substring($stage.Length)
  $destination=[IO.Path]::GetFullPath((Join-Path $target $relative))
  if(!$destination.StartsWith($target,[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid destination'}
  if($file.Name -in @('.env','tokens.json','jwt.secret','launcher.config.json','updater.json')){throw 'Package contains local configuration'}
  $saved=Join-Path $backup $relative
  if(Test-Path -LiteralPath $destination){New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null;Copy-Item -LiteralPath $destination -Destination $saved}
 }
 foreach($file in $files){
  $relative=$file.FullName.Substring($stage.Length)
  $destination=[IO.Path]::GetFullPath((Join-Path $target $relative))
  New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
  $changed.Add($relative)
  for($attempt=0;$attempt -lt 20;$attempt++){
   try{Copy-Item -LiteralPath $file.FullName -Destination $destination -Force;break}
   catch{if($attempt -eq 19){throw};Start-Sleep -Milliseconds 500}
  }
  if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $destination).Hash){throw ('Installed file verification failed: '+$relative)}
 }
 $version=[Reflection.AssemblyName]::GetAssemblyName((Join-Path $target 'SyndicateLauncher.dll')).Version.ToString()
 Write-UpdateLog ('Launcher files verified: '+$version+'. Match components will be verified on startup.')
 if($config.repair){
  $relative='repair.pending';$destination=Join-Path $target $relative;$saved=Join-Path $backup $relative
  if(Test-Path -LiteralPath $destination){Copy-Item -LiteralPath $destination -Destination $saved -Force}
  $changed.Add($relative)
  [IO.File]::WriteAllText($destination,'1')
 }
 if(!$NoLaunch){Start-Process -FilePath (Join-Path $target 'SyndicateLauncher.exe') -WorkingDirectory $target}
 exit 0
} catch {
 $failure=$_.Exception.Message
 for($i=$changed.Count-1;$i -ge 0;$i--){
  $relative=$changed[$i];$destination=[IO.Path]::GetFullPath((Join-Path $target $relative));$saved=Join-Path $backup $relative
  if(!$destination.StartsWith($target,[StringComparison]::OrdinalIgnoreCase)){continue}
  try{if(Test-Path -LiteralPath $saved){Copy-Item -LiteralPath $saved -Destination $destination -Force}else{Remove-Item -LiteralPath $destination -Force -ErrorAction Stop}}
  catch{Write-UpdateLog ('Rollback needs attention: '+$relative+' '+$_.Exception.Message)}
 }
 Write-UpdateLog ('INSTALL FAILED: '+$failure)
 if(!$NoLaunch){Add-Type -AssemblyName PresentationFramework;[System.Windows.MessageBox]::Show("Syndicate was not updated.`n`n$failure`n`nClose all Syndicate windows, then run RepairSyndicate.cmd from the extracted update.`n`nLog: $log",'Syndicate update failed') | Out-Null}
 Write-Error $failure
 exit 1
}
