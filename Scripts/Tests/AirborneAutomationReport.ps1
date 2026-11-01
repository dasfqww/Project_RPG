function Get-RPGAirborneAutomationTests {
 param([switch]$IncludeMovingPlatforms)
 $tests=@(@('CeilingWallAndNoLandingTimeout','DataAndValidation','FutureClockAndOwnedPoseResync',
   'ImmunityReplacementAndCleanse','LedgeWaitsForLowerFloor','LifetimeForeignMotionAndSecurity',
   'OwnerMoveClockBootstrap','PhysicalLandingAndRecovery','ReentrantResetInhibitionAndFloorLoss',
   'WalkableSlopeLanding') | ForEach-Object {'ProjectRPG.Combat.Airborne.'+$_})
 if($IncludeMovingPlatforms){
  $tests+=@('AirbornePlatform.BaseLossKeepsFalling','AirbornePlatform.CarryThroughRecovery',
   'AirbornePlatform.ForeignTeleportStillEndsRecovery','AirbornePlatformPlayer.BetweenMoveCarryKeepsForeignChecks',
   'AirbornePlatformPlayer.CarryPreservesSecurity','AirbornePlatformPlayer.PlayerOwnedBaseKeepsRawChecks',
   'AirbornePlatformPlayer.RejectsForeignTeleport','AirbornePlatformPlayer.RejectsRelativeCounterMotion')|
   ForEach-Object {'ProjectRPG.Combat.'+$_}
 }
 $tests
}

function Get-RPGAirborneAutomationFilter {
 param([switch]$IncludeMovingPlatforms)
 if($IncludeMovingPlatforms){'ProjectRPG.Combat.Airborne'}else{'ProjectRPG.Combat.Airborne.'}
}

function Test-RPGAirborneAutomationReport {
 param([string]$ReportFile,[string]$LogFile,[int]$ExitCode,[switch]$IncludeMovingPlatforms)
 if($ExitCode -ne 0){throw "Native process exit was $ExitCode"}
 $required=@(Get-RPGAirborneAutomationTests -IncludeMovingPlatforms:$IncludeMovingPlatforms)
 $filter=Get-RPGAirborneAutomationFilter -IncludeMovingPlatforms:$IncludeMovingPlatforms
 $report=Get-Content -LiteralPath $ReportFile -Raw -Encoding UTF8|ConvertFrom-Json
 foreach($key in @('succeeded','succeededWithWarnings','failed','notRun','inProcess','tests')){
  if(!$report.PSObject.Properties[$key]){throw "Missing report field: $key"}
 }
 if($report.succeeded -ne $required.Count -or $report.succeededWithWarnings -ne 0 -or $report.failed -ne 0 -or $report.notRun -ne 0 -or $report.inProcess -ne 0){
  throw 'Incomplete or failed native report summary'
 }
 if(@($report.tests).Count -ne $required.Count){throw 'Unexpected native report test count'}
 foreach($name in $required){
  $matches=@($report.tests|Where-Object {$_.fullTestPath -ceq $name})
  if($matches.Count -ne 1 -or $matches[0].state -cne 'Success' -or $matches[0].errors -ne 0){
   throw "Required native test missing, duplicated or failed: $name"
  }
 }
 $log=Get-Content -LiteralPath $LogFile -Raw -Encoding UTF8
 $discovery=[regex]::Match($log,("(?m)^.*Found $($required.Count) automation tests based on '"+[regex]::Escape($filter)+"'.*$"))
 if(!$discovery.Success){throw 'Missing airborne automation discovery'}
 $completed=[regex]::Matches($log,'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>[^}]+)\}')
 if($completed.Count -ne $required.Count){throw 'Unexpected native completion log count'}
 foreach($name in $required){
  if(@($completed|Where-Object {$_.Groups['path'].Value -ceq $name -and $_.Groups['result'].Value -ceq 'Success'}).Count -ne 1){
   throw "Required native completion missing or failed: $name"
  }
 }
 $beforeErrors=[regex]::Matches($log.Substring(0,$discovery.Index),'(?m)^.*: Error:.*$').Count
 $afterErrors=[regex]::Matches($log.Substring($discovery.Index),'(?m)^.*: Error:.*$').Count
 if($afterErrors -or $log -match 'Fatal error:|Assertion failed:|Ensure condition failed:'){throw 'Native execution logged errors'}
 [pscustomobject]@{passed=$true;tests=$required;testFilter=$filter;includeMovingPlatforms=[bool]$IncludeMovingPlatforms;nativeExit=$ExitCode;preDiscoveryErrors=$beforeErrors;postDiscoveryErrors=$afterErrors}
}
