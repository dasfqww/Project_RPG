[CmdletBinding()]
param(
    [string] $EngineRoot = 'D:\UE_5.8',
    [ValidateRange(20, 900)] [int] $TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
$editorPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editorPath -PathType Leaf)) {
    throw "Missing editor: $editorPath"
}
$modulePath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
if (-not (Test-Path -LiteralPath $modulePath)) {
    throw 'Build Project_RPGEditor Win64 Development before running this test.'
}

$runRoot = Join-Path $projectRoot ('Saved\Tests\Security\Automation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$userPath = Join-Path $runRoot 'User'
$process = $null
$moduleHashBefore = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
try {
    $arguments = @(
        ('"' + $projectPath + '"'), '/Engine/Maps/Entry',
        '-ExecCmds="Automation RunTests ProjectRPG.Security."',
        '-TestExit="Automation Test Queue Empty"',
        '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi', '-nosteam', '-DisablePython',
        '-DDC=(EnginePak,Local)',
        ('-UserDir="' + $userPath + '"'), ('-abslog="' + $logPath + '"')
    )
    $process = Start-Process -FilePath $editorPath -ArgumentList $arguments -PassThru -WindowStyle Hidden
    Write-Host "Started security automation (PID $($process.Id)): $logPath"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) {
            throw "Security automation timed out; inspect $logPath"
        }
        Start-Sleep -Milliseconds 250
    }
    if ($process.ExitCode -ne 0) {
        throw "Editor exited with code $($process.ExitCode); inspect $logPath"
    }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) {
        throw "Automation log missing: $logPath"
    }

    # UE's TestExit can return zero even if an assertion failed or no tests ran.
    $log = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    $results = [regex]::Matches($log,
        'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>ProjectRPG\.Security\.[^}]+)\}')
    if ($results.Count -eq 0) {
        throw "No security tests completed; inspect $logPath"
    }
    $passed = @($results | Where-Object { $_.Groups['result'].Value -eq 'Success' } |
        ForEach-Object { $_.Groups['path'].Value })
    if ($passed.Count -ne $results.Count -or
        $log -match 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:') {
        throw "Security automation/runtime failure; inspect $logPath"
    }
    foreach ($requiredTest in @(
        'ProjectRPG.Security.SkillDamage.FrozenTableDamage',
        'ProjectRPG.Security.SkillDamage.EventsUseServerQueryAndBudget',
        'ProjectRPG.Security.SkillDamage.RejectedMagnitudeAndRangePreserveHealth',
        'ProjectRPG.Security.SkillDamage.ReentrantEffectCannotSpendTwice',
        'ProjectRPG.Security.SkillDamage.EffectEndStopsRemainingHits',
        'ProjectRPG.Security.SkillDamage.EffectReactivationCannotInheritOldHits',
        'ProjectRPG.Security.SkillDamage.FinalDamageRejectsAmplification',
        'ProjectRPG.Security.SkillDamage.FinalDamageExactAndGlobalCeilings',
        'ProjectRPG.Security.SkillDamage.FinalDamageIgnoresForgedMetadata',
        'ProjectRPG.Security.SkillDamage.FinalDamageRejectsExtraEffectPaths',
        'ProjectRPG.Security.SkillDamage.FinalDamageReceiptIsolationAndReplay',
        'ProjectRPG.Security.SkillDamage.FinalDamageInvalidStatsAndProfiles',
        'ProjectRPG.Security.SkillDamage.DeadCombatantsRejectExecution',
        'ProjectRPG.Security.SkillDamage.LethalHitRejectsCorpseReplay',
        'ProjectRPG.Security.SkillDamage.DestroyedHitCannotAliasReplacement',
        'ProjectRPG.Security.Telemetry.QueuePreservesReservedBatchForRetry',
        'ProjectRPG.Security.Telemetry.QueueRemovesOnlyAcknowledgedBatch',
        'ProjectRPG.Security.Telemetry.QueueSaturationProtectsCriticalEvidence',
        'ProjectRPG.Security.Telemetry.NewEventsPreserveFlushAndRetryDeadlines',
        'ProjectRPG.Security.Telemetry.SerializesBackendBatchContract',
        'ProjectRPG.Security.Movement.AcceptsServerTimeJitterAndResumesAfterGap',
        'ProjectRPG.Security.TargetData.OrderedPredictionKeys',
        'ProjectRPG.Security.TargetData.BoundedRequestWindow',
        'ProjectRPG.Security.TargetData.RuntimeAdmission',
        'ProjectRPG.Security.TargetData.PayloadAndRateAdmission',
        'ProjectRPG.Security.TargetData.LiteralTransformCodec',
        'ProjectRPG.Security.Skill.AuthoritativeTermination'
    )) {
        if ($passed -notcontains $requiredTest) {
            throw "Missing successful result for $requiredTest; rebuild the editor and inspect $logPath"
        }
    }
    $moduleHashAfter = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
    if ($moduleHashBefore -ne $moduleHashAfter) {
        throw 'Editor module changed during security automation; rerun on one unchanged build.'
    }
    $proof = [ordered]@{
        schemaVersion = 1
        editorModuleSHA256 = $moduleHashBefore
        passedTests = $passed
        logPath = $logPath
        scope = [ordered]@{
            nativeSecurityAutomation = $true
            authoredBlueprintExecution = $false
            networkReplication = $false
            shippingRuntime = $false
        }
    }
    $proofPath = Join-Path $runRoot 'Security.proof.json'
    $proof | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    Write-Host "PASS: $($passed.Count) security tests. Log: $logPath; proof: $proofPath"
}
finally {
    if ($null -ne $process) {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
        }
        $process.Dispose()
    }
}
