# Launch the common client or camera development map after a normal Editor build.
# Uses a separate interactive game process with process-local development settings.
[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [string]$ProjectRoot = '',
    [switch]$PrintCommand,
    [switch]$CameraPresentation
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $PSScriptRoot '..\..' }
$developmentRoot = [IO.Path]::GetFullPath($ProjectRoot)
$developmentProject = Join-Path $developmentRoot 'Project_RPG.uproject'
$developmentEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$developmentMap = '/Game/System/Experiences/Development/Maps/L_ExperienceDevelopment'
$developmentMapFile = Join-Path $developmentRoot 'Content\System\Experiences\Development\Maps\L_ExperienceDevelopment.umap'
if ($CameraPresentation) {
    $developmentMap = '/Game/System/Experiences/Development/Camera/Maps/L_CameraExperienceDevelopment'
    $developmentMapFile = Join-Path $developmentRoot 'Content\System\Experiences\Development\Camera\Maps\L_CameraExperienceDevelopment.umap'
}
foreach ($required in @($developmentProject, $developmentEditor, $developmentMapFile)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
$developmentArguments = @(
    $developmentProject, $developmentMap, '-game', '-windowed', '-ResX=1280', '-ResY=720',
    '-nop4', '-nosplash', '-nosteam', '-NoLiveCoding', '-multiprocess',
    '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/Engine.GameInstance',
    '-ini:Engine:[/Script/Engine.Engine]:GameViewportClientClassName=/Script/CommonUI.CommonGameViewportClient',
    '-ini:Engine:[/Script/Engine.Engine]:LocalPlayerClassName=/Script/Engine.LocalPlayer',
    '-ini:Input:[/Script/Engine.InputSettings]:DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput',
    '-ini:Input:[/Script/Engine.InputSettings]:DefaultInputComponentClass=/Script/Project_RPG.RPGInputComponent'
)
if ($PrintCommand) {
    Write-Output ('& "' + $developmentEditor + '" ' + (($developmentArguments | ForEach-Object { '"' + $_ + '"' }) -join ' '))
    return
}
& $developmentEditor @developmentArguments
if ($LASTEXITCODE -ne 0) { throw "Development game exited with code $LASTEXITCODE" }
