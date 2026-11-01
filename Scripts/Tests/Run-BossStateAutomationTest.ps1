[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(20, 900)][int]$TimeoutSeconds = 300,
    [switch]$SkipSDKSetup
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$editorPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectPath = Join-Path $projectRoot 'Project_RPG.uproject'
if (-not (Test-Path -LiteralPath $editorPath)) { throw "Missing editor: $editorPath" }
$modulePath = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
if (-not (Test-Path -LiteralPath $modulePath)) {
    throw 'Build Project_RPGEditor Win64 Development before running this test.'
}
$runRoot = Join-Path $projectRoot ('Saved\Tests\BossState\Automation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$logPath = Join-Path $runRoot 'automation.log'
$cachePath = Join-Path $projectRoot 'Saved\Tests\BossState\DerivedDataCache'
$userPath = Join-Path $runRoot 'User'
$previousSkipSDKSetup = $env:UE_SKIP_UBT_SDK_SETUP
$process = $null
$moduleHashBefore = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash
try {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = '1' }
    $arguments = @(
        ('"' + $projectPath + '"'), '/Engine/Maps/Entry',
        '-ExecCmds="Automation RunTests ProjectRPG.Boss"', '-TestExit="Automation Test Queue Empty"',
        '-unattended', '-nop4', '-nosplash', '-nosound', '-nullrhi', '-nosteam', '-DisablePython',
        '-DDC=(EnginePak,Local)', ('-LocalDataCachePath="' + $cachePath + '"'),
        ('-UserDir="' + $userPath + '"'), ('-abslog="' + $logPath + '"')
    )
    $process = Start-Process -FilePath $editorPath -ArgumentList $arguments -PassThru -WindowStyle Hidden
    Write-Host "Started boss automation (PID $($process.Id)): $logPath"
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) { throw "Automation timed out; inspect $logPath" }
        Start-Sleep -Milliseconds 250
    }
    if ($process.ExitCode -ne 0) { throw "Editor exited with code $($process.ExitCode); inspect $logPath" }
    if (-not (Test-Path -LiteralPath $logPath)) { throw "Automation log missing: $logPath" }
    # UE's TestExit can return zero even when an automation assertion fails.
    $log = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
    $results = [regex]::Matches($log,
        'Test Completed\. Result=\{(?<result>[^}]+)\} Name=\{[^}]+\} Path=\{(?<path>ProjectRPG\.Boss\.[^}]+)\}')
    if ($results.Count -eq 0 -or
        @($results | Where-Object { $_.Groups['result'].Value -ne 'Success' }).Count -gt 0) {
        throw "Boss tests missing or unsuccessful; inspect $logPath"
    }
    $passed = @($results | ForEach-Object { $_.Groups['path'].Value })
    foreach ($testName in @('Definition', 'Lifecycle', 'NPCStartup', 'Threat', 'Ability', 'PhasePatterns', 'Damage', 'AreaStrike', 'Hazard', 'Dash', 'Summon', 'Enrage', 'BattleSession', 'RaidContent', 'Presentation', 'BattleRetry', 'DodgeDirection', 'PlayerAttack', 'WeaponSweep', 'SkillDamage', 'AttackScenario', 'LegacyAOE')) {
        $marker = 'Test Completed. Result={Success} Name={' + $testName + '} Path={ProjectRPG.Boss.' + $testName + '}'
        if (-not (Select-String -LiteralPath $logPath -SimpleMatch $marker -Quiet)) {
            throw "Missing successful result for ProjectRPG.Boss.$testName; inspect $logPath"
        }
    }
    if (Select-String -LiteralPath $logPath -Pattern 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:' -Quiet) {
        throw "Automation/runtime failure; inspect $logPath"
    }
    if ($moduleHashBefore -ne (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash) {
        throw 'Editor module changed during boss automation; rerun on one unchanged build.'
    }
    $proof = [ordered]@{
        schemaVersion = 1
        editorModuleSHA256 = $moduleHashBefore
        passedTests = $passed
        logPath = $logPath
        scope = [ordered]@{
            nativeBossAutomation = $true
            isolatedAuthoredRaidContent = $true
            phasePatternPriorityAndFreezeVerified = $true
            networkReplication = $false
            shippingRuntime = $false
        }
    }
    $proofPath = Join-Path $runRoot 'Boss.proof.json'
    $proof | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $proofPath -Encoding UTF8
    Write-Host "PASS: $($passed.Count) boss tests including Damage and RaidContent. Log: $logPath; proof: $proofPath"
}
finally {
    if ($SkipSDKSetup) { $env:UE_SKIP_UBT_SDK_SETUP = $previousSkipSDKSetup }
    if ($null -ne $process) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -ErrorAction SilentlyContinue }
        $process.Dispose()
    }
}
