[CmdletBinding()]
param(
 [string]$ProjectFile=(Join-Path $PSScriptRoot '../../Project_RPG.uproject'),
 [string]$EngineRoot='D:/UE_5.8',
 [ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedModuleSHA256,
 [ValidateRange(30,600)][int]$TimeoutSeconds=180,
 [string]$OutputRoot=(Join-Path $PSScriptRoot '../../Saved/Tests/AirborneAutomation'),
 [switch]$IncludeMovingPlatforms
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'AirborneAutomationReport.ps1')
. (Join-Path $PSScriptRoot '../Network/GuardianMelee2Artifacts.ps1')
$ProjectFile=[IO.Path]::GetFullPath($ProjectFile)
$sourceRoot=Split-Path -Parent $ProjectFile
$run=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ([Guid]::NewGuid().ToString('N'))
$temp=Join-Path $run 'Temp'
$runtime=Join-Path $temp 'RuntimeProject'
$moduleName='UnrealEditor-Project_RPG.dll'
$sourceModule=Join-Path $sourceRoot ('Binaries/Win64/'+$moduleName)
$module=Join-Path $runtime ('Binaries/Win64/'+$moduleName)
$logPath=Join-Path $run 'Native.log'
$reportPath=Join-Path $run 'Report/index.json'
$process=$null;$exitCode=$null;$loaded=$null;$failure=$null;$cleanup=$null;$audit=$null;$moduleHash=$null
$before=@();$arguments=@();$started=$false
$required=@(Get-RPGAirborneAutomationTests -IncludeMovingPlatforms:$IncludeMovingPlatforms)
$filter=Get-RPGAirborneAutomationFilter -IncludeMovingPlatforms:$IncludeMovingPlatforms
New-Item -ItemType Directory -Path $run -Force|Out-Null
function Get-InputRecord([string]$Path){
 $file=Get-Item -LiteralPath $Path -ErrorAction Stop
 [pscustomobject]@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
}
try{
 $moduleHash=(Get-FileHash -LiteralPath $sourceModule -Algorithm SHA256).Hash
 if($ExpectedModuleSHA256 -and $ExpectedModuleSHA256 -ne $moduleHash){throw 'Selected module hash differs from -ExpectedModuleSHA256; no UE process started.'}
 $paths=@($ProjectFile)
 $sourcePaths=@('Public/Combat/RPGAirborneTypes.h','Private/Component/Combat/RPGCombatAirborne.cpp','Private/Tests/RPGAirborneTests.cpp')
 if($IncludeMovingPlatforms){$sourcePaths+=@('Public/Component/Combat/RPGCombatControlComponent.h',
  'Public/Component/RPGCharacterMovementComponent.h','Private/Component/RPGCharacterMovementComponent.cpp',
  'Public/Component/RPGSecurityValidationComponent.h','Private/Component/RPGSecurityValidationComponent.cpp')}
 foreach($relative in $sourcePaths){
  $paths+=Join-Path $sourceRoot ('Source/Project_RPG/'+$relative)
 }
 $paths+=@($PSCommandPath,(Join-Path $PSScriptRoot 'AirborneAutomationReport.ps1'),(Join-Path $PSScriptRoot '../Network/GuardianMelee2Artifacts.ps1'))
 $paths+=@(Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'Config') -File -Recurse|ForEach-Object {$_.FullName})
 $paths+=@(Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'Binaries/Win64') -File|Where-Object {$_.Extension -eq '.dll' -or $_.Name -eq 'UnrealEditor.modules'}|ForEach-Object {$_.FullName})
 $before=@($paths|ForEach-Object {Get-InputRecord $_})
 New-Item -ItemType Directory -Path (Join-Path $runtime 'Binaries/Win64') -Force|Out-Null
 Copy-Item -LiteralPath $ProjectFile -Destination (Join-Path $runtime 'Project_RPG.uproject')
 Copy-Item -LiteralPath (Join-Path $sourceRoot 'Config') -Destination $runtime -Recurse
 foreach($name in @('Content','Plugins')){New-Item -ItemType Junction -Path (Join-Path $runtime $name) -Target (Join-Path $sourceRoot $name)|Out-Null}
 foreach($record in $before|Where-Object {[IO.Path]::GetDirectoryName($_.path) -eq [IO.Path]::GetFullPath((Join-Path $sourceRoot 'Binaries/Win64'))}){
  $copy=Join-Path $runtime ('Binaries/Win64/'+[IO.Path]::GetFileName($record.path))
  Copy-Item -LiteralPath $record.path -Destination $copy
  if((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $record.sha256){throw 'Module snapshot copy mismatch'}
 }
 if((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -ne $moduleHash){throw 'Module changed during snapshot preparation'}
 $arguments=@(('"'+(Join-Path $runtime 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry',
  ('-ExecCmds="Automation RunTests '+$filter+'"'),'-TestExit="Automation Test Queue Empty"',
  '-unattended','-nullrhi','-nosound','-nosteam','-nop4','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-DDC=(EnginePak,Local)',
  ('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+$logPath+'"'),('-ReportExportPath="'+(Join-Path $run 'Report')+'"'),
  '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False')
 @{project=$ProjectFile;moduleSHA256=$moduleHash;testFilter=$filter;includeMovingPlatforms=[bool]$IncludeMovingPlatforms;requiredTests=$required;sourceAndModuleInputs=$before;arguments=$arguments;contentAndPluginsShared=$true}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $run 'Invocation.json') -Encoding UTF8
 $process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru
 $started=$true;$null=$process.Handle
 Write-Host "Airborne native PID=$($process.Id) Run=$run"
 $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
 while(!$process.HasExited){
  if(!$loaded){
   try{
    $candidate=@((Get-Process -Id $process.Id -ErrorAction Stop).Modules|Where-Object {$_.ModuleName -eq $moduleName})
    if($candidate.Count -eq 1){$loaded=Get-InputRecord $candidate[0].FileName}
   }catch{} # Retry while the process is loading its modules.
  }
  if([DateTime]::UtcNow -ge $deadline){throw 'Owned airborne native process timed out'}
  $process.WaitForExit(500)|Out-Null
 }
 $process.WaitForExit();$exitCode=$process.ExitCode
 if($null -eq $exitCode){throw 'Native process exit code was not captured'}
 if(!$loaded -or $loaded.path -ne [IO.Path]::GetFullPath($module) -or $loaded.sha256 -ne $moduleHash){throw 'Expected runtime module was not observed in the native process'}
 $audit=Test-RPGAirborneAutomationReport $reportPath $logPath $exitCode -IncludeMovingPlatforms:$IncludeMovingPlatforms
}catch{$failure=$_.Exception.Message}finally{
 $released=$true
 if($process){
  try{
   if(!$process.HasExited){
    $owned=Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    if(!$owned -or !$owned.CommandLine -or !$owned.CommandLine.Contains((Split-Path $run -Leaf)) -or !$owned.CommandLine.Contains('Automation RunTests '+$filter)){throw 'Cannot confirm native process ownership; Temp retained'}
    Stop-Process -Id $process.Id -ErrorAction Stop
   }
   if(!$process.WaitForExit(10000)){throw 'Owned native process still holds its files'}
  }catch{$released=$false;$failure=([string]$failure+'; '+$_.Exception.Message).Trim(';',' ')}
 }
 foreach($record in $before){
  try{if((Get-InputRecord $record.path).sha256 -ne $record.sha256){throw "Input changed: $($record.path)"}}catch{$failure=([string]$failure+'; '+$_.Exception.Message).Trim(';',' ')}
 }
 try{
  if(Test-Path -LiteralPath $module){if((Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -ne $moduleHash){throw 'Runtime module changed'}}
 }catch{$failure=([string]$failure+'; '+$_.Exception.Message).Trim(';',' ')}
 if($released){try{$cleanup=Remove-GuardianProbeArtifact $temp $run}catch{$failure=([string]$failure+'; Cleanup: '+$_.Exception.Message).Trim(';',' ')}}
 if(!(Test-Path -LiteralPath (Join-Path $run 'Invocation.json'))){@{project=$ProjectFile;moduleSHA256=$moduleHash;testFilter=$filter;includeMovingPlatforms=[bool]$IncludeMovingPlatforms;requiredTests=$required;sourceAndModuleInputs=$before;arguments=$arguments}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $run 'Invocation.json') -Encoding UTF8}
 @{passed=(!$failure -and $null -ne $audit);failure=$failure;nativeStarted=$started;nativeExit=$exitCode;report=$audit;loadedModule=$loaded;moduleSHA256=$moduleHash;cleanup=$cleanup;temporaryArtifactsRemoved=(!(Test-Path -LiteralPath $temp));historicalHashesOnly=$true;gameBuildRun=$false;gpu=$false;iris=$false}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $run 'Verification.proof.json') -Encoding UTF8
}
Write-Output "Evidence=$run"
if($failure){throw $failure}
Write-Output "PASS $($required.Count) airborne native tests; Temp removed, bytes=$($cleanup.deletedBytes)"
