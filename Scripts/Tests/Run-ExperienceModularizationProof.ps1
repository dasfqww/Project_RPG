# Runs only opt-in checks in separate processes with one private settings file across restarts.
[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateSet('Write','Restore','VerifyDefault','Players')][string[]]$Modes = @('Write','Restore','VerifyDefault','Players'),
    [ValidateRange(100,600)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $PSScriptRoot '..\..' }
$proofRoot = [IO.Path]::GetFullPath($ProjectRoot)
$proofEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$proofProject = Join-Path $proofRoot 'Project_RPG.uproject'
$proofMap = '/Game/System/Experiences/Development/Camera/Maps/L_CameraExperienceDevelopment'
$proofId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$proofOutput = Join-Path $proofRoot ('Saved\Logs\ExperienceModularization-' + $proofId)
foreach ($required in @($proofProject,$proofEditor,(Join-Path $proofRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
New-Item -ItemType Directory -Path $proofOutput | Out-Null
$proofSettings = Join-Path $proofOutput 'Settings\GameUserSettings.ini'
New-Item -ItemType Directory -Path (Split-Path $proofSettings -Parent) | Out-Null
$proofResults = @()
foreach ($mode in $Modes) {
    $roleOutput = Join-Path $proofOutput $mode
    New-Item -ItemType Directory -Path $roleOutput | Out-Null
    $arguments = @(
        $proofProject,$proofMap,'-game','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720',
        '-unattended','-nop4','-nosplash','-nosound','-nosteam','-NoLiveCoding','-DisablePython',
        '-multiprocess','-MultiprocessSaveConfig','-RPGModularizationProof',
        ('-RPGModularizationProofMode=' + $mode),('-RPGModularizationProofOutput=' + $roleOutput),
        '-ExecCmds=RPG.Experience.ModularizationProof,t.MaxFPS 30',
        ('-abslog=' + (Join-Path $roleOutput 'Game.log')),('-UserDir=' + (Join-Path $proofOutput 'User')),
        ('-GAMEUSERSETTINGSINI=' + $proofSettings),
        '-DDC=(InstalledEnginePak,ZenLocal,Local)',
        '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/Engine.GameInstance',
        '-ini:Engine:[/Script/Engine.Engine]:GameViewportClientClassName=/Script/CommonUI.CommonGameViewportClient',
        '-ini:Engine:[/Script/Engine.Engine]:LocalPlayerClassName=/Script/Engine.LocalPlayer',
        '-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=Null',
        '-ini:Input:[/Script/Engine.InputSettings]:DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput',
        '-ini:Input:[/Script/Engine.InputSettings]:DefaultInputComponentClass=/Script/Project_RPG.RPGInputComponent'
    )
    $quoted = @($arguments | ForEach-Object {
        if ($_.Contains('"')) { throw 'Unexpected embedded quote in process argument' }
        '"' + $_ + '"'
    })
    $process = Start-Process -FilePath $proofEditor -ArgumentList $quoted -PassThru -WindowStyle Hidden
    Write-Output ("Started " + $mode + ", PID " + $process.Id + ", output: " + $roleOutput)
    try {
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { throw "Modularization check timed out: $mode" }
        $process.Refresh()
        if ($process.ExitCode -ne 0) { throw "Modularization check failed: $mode / exit $($process.ExitCode)" }
        $report = Get-Content -LiteralPath (Join-Path $roleOutput 'Report.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not $report.Passed) { throw "Native report failed: $mode / $($report.Failure)" }
        if ([IO.Path]::GetFullPath($report.GameUserSettingsIni) -ne [IO.Path]::GetFullPath($proofSettings)) {
            throw 'Check did not use the private settings file'
        }
        $log = Get-Content -LiteralPath (Join-Path $roleOutput 'Game.log') -Raw -Encoding UTF8
        if ($log -match 'Fatal error:|Assertion failed:|Ensure condition failed:') { throw "Runtime fatal/assert/ensure: $mode" }
        $proofResults += $report
        [ordered]@{Passed=$true;Settings=$proofSettings;Results=$proofResults} |
            ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $proofOutput 'Summary.json') -Encoding UTF8
        Write-Output ("PASS " + $mode + ": " + ($report.Checks -join ', '))
    } finally {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
Write-Output ("Modularization checks passed: " + $proofOutput)
