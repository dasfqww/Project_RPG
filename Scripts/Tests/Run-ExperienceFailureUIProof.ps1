# Render Experience failure scenarios in separate, opt-in proof processes.
[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(75,600)][int]$TimeoutSeconds = 180,
    [ValidateSet('InvalidId','MissingAsset','NoticeDisabled','IndependentLoadingHold')]
    [string[]]$Cases = @('InvalidId','MissingAsset','NoticeDisabled','IndependentLoadingHold')
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $PSScriptRoot '..\..' }
$failureRoot = [IO.Path]::GetFullPath($ProjectRoot)
$failureProject = Join-Path $failureRoot 'Project_RPG.uproject'
$failureEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$failureMap = '/Game/System/Experiences/Development/Maps/L_ExperienceDevelopment'
$failureId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$failureOutput = Join-Path $failureRoot ('Saved\Logs\ExperienceFailureUI-' + $failureId)
foreach ($required in @($failureProject,$failureEditor)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
New-Item -ItemType Directory -Path $failureOutput -Force | Out-Null
$failureCases = @(
    @{Name='InvalidId'; Selection='RPGExperienceDefinition:DA_FailureUIAbsent'; Extra=@(); Reason='Invalid experience:'},
    @{Name='MissingAsset'; Selection='/Game/System/Experiences/DA_FailureUIAbsent.DA_FailureUIAbsent'; Extra=@(); Reason='Selected asset is not an RPG experience'},
    @{Name='NoticeDisabled'; Selection='/Game/System/Experiences/DA_FailureUIAbsent.DA_FailureUIAbsent'; Extra=@('-RPGExperienceFailureUIProofNoNotice','-ini:Game:[/Script/Project_RPG.RPGExperienceSettings]:bShowLoadingFailureNotice=False'); Reason='Selected asset is not an RPG experience'},
    @{Name='IndependentLoadingHold'; Selection='/Game/System/Experiences/DA_FailureUIAbsent.DA_FailureUIAbsent'; Extra=@('-RPGExperienceFailureUIProofHold'); Reason='Selected asset is not an RPG experience'}
)
$failureCases = @($failureCases | Where-Object { $Cases -contains $_.Name })
$failureResults = @()
foreach ($case in $failureCases) {
    $caseOutput = Join-Path $failureOutput $case.Name
    New-Item -ItemType Directory -Path $caseOutput -Force | Out-Null
    $caseArguments = @(
        $failureProject, $failureMap, '-game', '-windowed', '-ResX=1280', '-ResY=720',
        '-RenderOffscreen', '-unattended', '-nop4', '-nosplash', '-nosteam',
        '-NoLiveCoding', '-multiprocess', '-DisablePython',
        '-RPGExperienceFailureUIProof', ('-RPGExperience=' + $case.Selection),
        ('-RPGExperienceUIProofOutput=' + $caseOutput), '-ExecCmds=RPG.Experience.FailureUIProof',
        ('-abslog=' + (Join-Path $caseOutput 'Game.log')),
        '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/Engine.GameInstance',
        '-ini:Engine:[/Script/Engine.Engine]:GameViewportClientClassName=/Script/CommonUI.CommonGameViewportClient',
        '-ini:Engine:[/Script/Engine.Engine]:LocalPlayerClassName=/Script/Engine.LocalPlayer',
        '-ini:Input:[/Script/Engine.InputSettings]:DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput',
        '-ini:Input:[/Script/Engine.InputSettings]:DefaultInputComponentClass=/Script/Project_RPG.RPGInputComponent'
    ) + $case.Extra
    $caseQuoted = @($caseArguments | ForEach-Object {
        if ($_.Contains('"')) { throw 'Unexpected quote in process argument' }
        '"' + $_ + '"'
    })
    $caseProcess = Start-Process -FilePath $failureEditor -ArgumentList $caseQuoted -PassThru -WindowStyle Hidden
    Write-Output ("Started " + $case.Name + ", PID " + $caseProcess.Id + ", output: " + $caseOutput)
    try {
        if (-not $caseProcess.WaitForExit($TimeoutSeconds * 1000)) { throw ('Scenario timed out: ' + $case.Name) }
        $caseProcess.Refresh()
        $caseExit = $caseProcess.ExitCode
        [ordered]@{ExitCode=$caseExit;CompletedAt=(Get-Date).ToString('o')} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $caseOutput 'ProcessResult.json') -Encoding UTF8
        if ($caseExit -ne 0) { throw ('Scenario process failed: ' + $case.Name) }
        $caseReport = Get-Content -LiteralPath (Join-Path $caseOutput 'Report.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $caseLog = Get-Content -LiteralPath (Join-Path $caseOutput 'Game.log') -Raw -Encoding UTF8
        if (-not $caseReport.Passed -or -not $caseReport.NoticeRemovedOnManagerEndPlay) {
            throw ('Scenario UI or cleanup check failed: ' + $case.Name)
        }
        if ($caseLog -match 'Fatal error:|Assertion failed:|Ensure condition failed:|RPG_EXPERIENCE READY') {
            throw ('Unexpected runtime error or Ready event: ' + $case.Name)
        }
        if (-not $caseReport.ExpectedInternalReason.Contains($case.Reason)) {
            throw ('Unexpected failure diagnostic: ' + $case.Name)
        }
        if ($case.Name -eq 'IndependentLoadingHold' -and -not $caseReport.IndependentLoadingHoldVerified) {
            throw 'Independent loading request was not verified'
        }
        $failureResults += [ordered]@{Case=$case.Name;Passed=$true;Output=$caseOutput}
        Write-Output ('Passed ' + $case.Name)
    } finally {
        if (-not $caseProcess.HasExited) { $caseProcess.Kill(); $caseProcess.WaitForExit() }
        $caseProcess.Dispose()
    }
}
[ordered]@{Passed=$true;Cases=$failureResults;CompletedAt=(Get-Date).ToString('o')} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $failureOutput 'Summary.json') -Encoding UTF8
Write-Output ('Failure UI proof passed selected cases: ' + $failureOutput)
