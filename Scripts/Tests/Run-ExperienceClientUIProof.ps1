# Prove client UI in a separate process with rendering, engine key input and Slate focus checks.
[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\UE_5.8',
    [ValidateRange(75,600)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $PSScriptRoot '..\..' }
$proofRoot = [IO.Path]::GetFullPath($ProjectRoot)
$proofProject = Join-Path $proofRoot 'Project_RPG.uproject'
$proofEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$proofMap = '/Game/System/Experiences/Development/Maps/L_ExperienceDevelopment'
$proofId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$proofOutput = Join-Path $proofRoot ('Saved\Logs\ExperienceClientUI-' + $proofId)
foreach ($required in @($proofProject,$proofEditor)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
New-Item -ItemType Directory -Path $proofOutput -Force | Out-Null
$proofArguments = @(
    $proofProject, $proofMap, '-game', '-windowed', '-ResX=1280', '-ResY=720',
    '-RenderOffscreen', '-unattended', '-nop4', '-nosplash', '-nosteam',
    '-NoLiveCoding', '-multiprocess', '-DisablePython',
    '-RPGExperienceUIProof', ('-RPGExperienceUIProofOutput=' + $proofOutput),
    '-ExecCmds=RPG.Experience.ClientUIProof', ('-abslog=' + (Join-Path $proofOutput 'Game.log')),
    '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/Engine.GameInstance',
    '-ini:Engine:[/Script/Engine.Engine]:GameViewportClientClassName=/Script/CommonUI.CommonGameViewportClient',
    '-ini:Engine:[/Script/Engine.Engine]:LocalPlayerClassName=/Script/Engine.LocalPlayer',
    '-ini:Input:[/Script/Engine.InputSettings]:DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput',
    '-ini:Input:[/Script/Engine.InputSettings]:DefaultInputComponentClass=/Script/Project_RPG.RPGInputComponent'
)
$proofQuoted = @($proofArguments | ForEach-Object {
    if ($_.Contains('"')) { throw 'Unexpected quote in process argument' }
    '"' + $_ + '"'
})
$proofProcess = Start-Process -FilePath $proofEditor -ArgumentList $proofQuoted -PassThru -WindowStyle Hidden
Write-Output ("Client UI proof process: " + $proofProcess.Id + ", output: " + $proofOutput)
try {
    if (-not $proofProcess.WaitForExit($TimeoutSeconds * 1000)) { throw 'Client UI proof timed out' }
    $proofProcess.Refresh()
    $proofExit = $proofProcess.ExitCode
    [ordered]@{ExitCode=$proofExit;Output=$proofOutput;CompletedAt=(Get-Date).ToString('o')} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $proofOutput 'ProcessResult.json') -Encoding UTF8
    if ($proofExit -ne 0) { throw "Client UI proof failed (exit $proofExit): $proofOutput" }
    $proofReport = Get-Content -LiteralPath (Join-Path $proofOutput 'Report.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $proofReport.Passed) { throw "Client UI proof did not pass: $proofOutput" }
    Write-Output ($proofReport | ConvertTo-Json -Depth 8)
} finally {
    if (-not $proofProcess.HasExited) { $proofProcess.Kill(); $proofProcess.WaitForExit() }
    $proofProcess.Dispose()
}
