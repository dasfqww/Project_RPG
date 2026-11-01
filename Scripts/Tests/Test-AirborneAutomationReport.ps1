[CmdletBinding()]
param([string]$HistoricalReport,[string]$HistoricalLog,[switch]$IncludeMovingPlatforms)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'AirborneAutomationReport.ps1')
. (Join-Path $PSScriptRoot '../Network/GuardianMelee2Artifacts.ps1')
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=Join-Path $workspace ('Saved/Tests/AirborneAutomation/RunnerChecks-'+[Guid]::NewGuid().ToString('N'))
$fixture=Join-Path $output 'Temp'
New-Item -ItemType Directory -Path $fixture -Force|Out-Null
$reportFile=Join-Path $fixture 'index.json';$logFile=Join-Path $fixture 'Native.log'
$checks=[Collections.Generic.List[string]]::new();$state=@{launches=0};$historical=$null;$cleanup=$null;$failure=$null
$sourceFile=Join-Path $workspace 'Source/Project_RPG/Private/Tests/RPGAirborneTests.cpp'
$sourceHash=(Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash
$source=Get-Content -LiteralPath $sourceFile -Raw -Encoding UTF8
$filter=Get-RPGAirborneAutomationFilter -IncludeMovingPlatforms:$IncludeMovingPlatforms
$expectedCount=if($IncludeMovingPlatforms){18}else{10}
$sourceNames=@([regex]::Matches($source,'IMPLEMENT_SIMPLE_AUTOMATION_TEST\([^,]+,\s*"(ProjectRPG\.Combat\.Airborne[^"]+)"')|ForEach-Object {$_.Groups[1].Value}|Where-Object {$_.StartsWith($filter,[StringComparison]::Ordinal)})
$valid=@{succeeded=$sourceNames.Count;succeededWithWarnings=0;failed=0;notRun=0;inProcess=0;tests=@($sourceNames|ForEach-Object {@{fullTestPath=$_;state='Success';errors=0}})}
$validLog="LogAutomationTest: Error: startup condition`nLogAutomationCommandLine: Display: Found $expectedCount automation tests based on '$filter'`n"
$validLog+=(@($sourceNames|ForEach-Object {"LogAutomationController: Display: Test Completed. Result={Success} Name={fixture} Path={$_}"}) -join "`n")
function Assert-Check([bool]$Condition,[string]$Name){if(!$Condition){throw $Name};$checks.Add($Name)}
function Write-Report($Value){$Value|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $reportFile -Encoding UTF8}
function Assert-Rejected([string]$Name,[scriptblock]$Edit,[scriptblock]$EditLog,[int]$ExitCode=0){
 $copy=$valid|ConvertTo-Json -Depth 7|ConvertFrom-Json
 if($Edit){& $Edit $copy}
 Write-Report $copy
 $text=$validLog
 if($EditLog){$text=& $EditLog $text}
 [IO.File]::WriteAllText($logFile,$text,[Text.Encoding]::UTF8)
 $rejected=$false
 try{$null=Test-RPGAirborneAutomationReport $reportFile $logFile $ExitCode -IncludeMovingPlatforms:$IncludeMovingPlatforms}catch{$rejected=$true}
 Assert-Check $rejected $Name
}
function Start-Process {$state.launches++;throw 'Unexpected UE launch in runner checks'}
try{
 Assert-Check ($sourceNames.Count -eq $expectedCount -and @(Compare-Object ($sourceNames|Sort-Object) (@(Get-RPGAirborneAutomationTests -IncludeMovingPlatforms:$IncludeMovingPlatforms)|Sort-Object)).Count -eq 0) "Required list matches all $expectedCount selected source test declarations"
 Write-Report $valid
 [IO.File]::WriteAllText($logFile,$validLog,[Text.Encoding]::UTF8)
 $result=Test-RPGAirborneAutomationReport $reportFile $logFile 0 -IncludeMovingPlatforms:$IncludeMovingPlatforms
 Assert-Check ($result.tests.Count -eq $expectedCount -and $result.testFilter -ceq $filter -and $result.preDiscoveryErrors -eq 1 -and $result.postDiscoveryErrors -eq 0) 'Complete report and log pass while startup errors remain separate'
 Assert-Rejected 'Reject empty report' {param($r);$r.tests=@()}
 Assert-Rejected 'Reject missing owner move-clock test' {param($r);$r.tests=@($r.tests|Where-Object {$_.fullTestPath -notlike '*.OwnerMoveClockBootstrap'})}
 Assert-Rejected 'Reject duplicate replacing a required test' {param($r);$r.tests[0].fullTestPath=$r.tests[1].fullTestPath}
 Assert-Rejected 'Reject failed test behind successful summary' {param($r);$r.tests[0].state='Fail'}
 Assert-Rejected 'Reject unrun test behind successful summary' {param($r);$r.tests[0].state='NotRun'}
 Assert-Rejected 'Reject per-test errors behind successful state' {param($r);$r.tests[0].errors=1}
 Assert-Rejected 'Reject stale summary count' {param($r);$r.succeeded--}
 Assert-Rejected 'Reject missing report summary field' {param($r);$r.PSObject.Properties.Remove('inProcess')}
 Assert-Rejected 'Reject extra test' {param($r);$r.tests+= $r.tests[0]}
 Assert-Rejected 'Reject missing completion in log' $null {param($text);$text -replace '(?m)^.*Path=\{ProjectRPG\.Combat\.Airborne\.OwnerMoveClockBootstrap\}.*$',''}
 Assert-Rejected 'Reject failed completion behind successful JSON' $null {param($text);$text.Replace('Result={Success} Name={fixture} Path={ProjectRPG.Combat.Airborne.OwnerMoveClockBootstrap}','Result={Fail} Name={fixture} Path={ProjectRPG.Combat.Airborne.OwnerMoveClockBootstrap}')}
 Assert-Rejected 'Reject post-discovery execution error' $null {param($text);$text+"`nLogTest: Error: execution failed"}
 Assert-Rejected 'Reject fatal even before discovery' $null {param($text);"Fatal error: fixture`n"+$text}
 Assert-Rejected 'Reject missing test discovery' $null {param($text);$text.Replace("Found $expectedCount automation tests",'Found 0 automation tests')}
 Assert-Rejected 'Reject nonzero process exit' $null $null 1
 $otherMode=!$IncludeMovingPlatforms
 $otherFilter=Get-RPGAirborneAutomationFilter -IncludeMovingPlatforms:$otherMode
 Assert-Rejected 'Reject discovery from the other selection mode' $null {param($text);$text.Replace("based on '$filter'","based on '$otherFilter'")}
 Write-Report $valid
 [IO.File]::WriteAllText($logFile,$validLog,[Text.Encoding]::UTF8)
 $rejected=$false
 try{$null=Test-RPGAirborneAutomationReport $reportFile $logFile 0 -IncludeMovingPlatforms:$otherMode}catch{$rejected=$true}
 Assert-Check $rejected 'Reject a successful report when audited as the other selection mode'
 if($IncludeMovingPlatforms){
  Assert-Rejected 'Reject missing between-move regression hidden behind a replacement' {param($r);($r.tests|Where-Object {$_.fullTestPath -like '*.BetweenMoveCarryKeepsForeignChecks'}).fullTestPath='ProjectRPG.Combat.AirbornePlatformPlayer.UnrelatedCase'}
  Assert-Rejected 'Reject platform foreign-teleport failure behind a successful summary' {param($r);($r.tests|Where-Object {$_.fullTestPath -eq 'ProjectRPG.Combat.AirbornePlatformPlayer.RejectsForeignTeleport'}).state='Fail'}
 }
 if($HistoricalReport -or $HistoricalLog){
  if(!$HistoricalReport -or !$HistoricalLog){throw 'Both historical report and log are required.'}
  $beforeReport=(Get-FileHash -LiteralPath $HistoricalReport -Algorithm SHA256).Hash
  $beforeLog=(Get-FileHash -LiteralPath $HistoricalLog -Algorithm SHA256).Hash
  $historical=Test-RPGAirborneAutomationReport $HistoricalReport $HistoricalLog 0 -IncludeMovingPlatforms:$IncludeMovingPlatforms
  Assert-Check ($historical.passed -and (Get-FileHash -LiteralPath $HistoricalReport -Algorithm SHA256).Hash -eq $beforeReport -and (Get-FileHash -LiteralPath $HistoricalLog -Algorithm SHA256).Hash -eq $beforeLog) "Read-only audit accepts preserved real $expectedCount-test report and log without modifying them"
  $historical|Add-Member NoteProperty reportSHA256 $beforeReport
  $historical|Add-Member NoteProperty logSHA256 $beforeLog
 }
 # Exercise live-runner preflight/finally with a deliberately wrong module hash.
 # The fixture module is plain text; it must never be executed.
 $project=Join-Path $fixture 'Project';$binaries=Join-Path $project 'Binaries/Win64'
 New-Item -ItemType Directory -Path $binaries -Force|Out-Null
 $projectFile=Join-Path $project 'Project_RPG.uproject'
 [IO.File]::WriteAllText($projectFile,'{}')
 $module=Join-Path $binaries 'UnrealEditor-Project_RPG.dll'
 [IO.File]::WriteAllText($module,'non-executable fixture')
 $moduleBefore=(Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
 foreach($preflightMode in @($false,$true)){
  $preflightRoot=Join-Path $fixture ('Runs-'+$preflightMode)
  $rejected=$false
  try{& (Join-Path $PSScriptRoot 'Run-AirborneAutomationTest.ps1') -ProjectFile $projectFile -ExpectedModuleSHA256 ('0'*64) -OutputRoot $preflightRoot -IncludeMovingPlatforms:$preflightMode|Out-Null}catch{
   if($_.Exception.Message -notlike 'Selected module hash differs*'){throw}
   $rejected=$true
  }
  $run=@(Get-ChildItem -LiteralPath $preflightRoot -Directory)[0].FullName
  $proof=Get-Content -LiteralPath (Join-Path $run 'Verification.proof.json') -Raw -Encoding UTF8|ConvertFrom-Json
  $invocation=Get-Content -LiteralPath (Join-Path $run 'Invocation.json') -Raw -Encoding UTF8|ConvertFrom-Json
  $preflightCount=if($preflightMode){18}else{10}
  $preflightFilter=Get-RPGAirborneAutomationFilter -IncludeMovingPlatforms:$preflightMode
  Assert-Check ($rejected -and !$proof.passed -and !$proof.nativeStarted -and $state.launches -eq 0) "Module hash mismatch fails before any UE launch; platforms=$preflightMode"
  Assert-Check ($proof.temporaryArtifactsRemoved -and $invocation.requiredTests.Count -eq $preflightCount -and $invocation.testFilter -ceq $preflightFilter -and $invocation.includeMovingPlatforms -eq $preflightMode -and (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash -eq $moduleBefore) "Preflight preserves selection and verdict/caller module and leaves no Temp; platforms=$preflightMode"
 }
 Assert-Check ((Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash -eq $sourceHash) 'Source declarations stayed unchanged during tool checks'
}catch{$failure=$_.Exception.Message;throw}finally{
 $cleanup=Remove-GuardianProbeArtifact $fixture $output
 @{passed=(!$failure);failure=$failure;checks=@($checks.ToArray());unrealLaunched=$false;includeMovingPlatforms=[bool]$IncludeMovingPlatforms;testFilter=$filter;selectedTests=$sourceNames;sourceSHA256=$sourceHash;historicalAudit=$historical;cleanup=$cleanup;temporaryArtifactsRemoved=(!(Test-Path -LiteralPath $fixture))}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $output 'RunnerChecks.proof.json') -Encoding UTF8
}
Write-Output "PASS $($checks.Count) runner/report checks; no UE launch; Evidence=$output"
