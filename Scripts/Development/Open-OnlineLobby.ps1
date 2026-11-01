# Launch the normal login/lobby flow. The backend must already be running.
[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [string]$ProjectRoot = '',
    [switch]$DevelopmentLogin,
    [switch]$PrintCommand
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $PSScriptRoot '..\..' }
$onlineLobbyRoot = [IO.Path]::GetFullPath($ProjectRoot)
$onlineLobbyProject = Join-Path $onlineLobbyRoot 'Project_RPG.uproject'
$onlineLobbyEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
foreach ($required in @($onlineLobbyProject, $onlineLobbyEditor, (Join-Path $onlineLobbyRoot 'Content\Maps\LobbyMap.umap'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
$onlineLobbyArguments = @(
    $onlineLobbyProject, '/Game/Maps/LobbyMap', '-game', '-windowed', '-ResX=1280', '-ResY=720',
    '-nop4', '-nosplash', '-NoLiveCoding', '-multiprocess'
)
if ($DevelopmentLogin) {
    $onlineLobbyArguments += @('-nosteam', '-RPGDevelopmentLogin')
}
if ($PrintCommand) {
    Write-Output ('& "' + $onlineLobbyEditor + '" ' + (($onlineLobbyArguments | ForEach-Object { '"' + $_ + '"' }) -join ' '))
    return
}
& $onlineLobbyEditor @onlineLobbyArguments
if ($LASTEXITCODE -ne 0) { throw "Online lobby exited with code $LASTEXITCODE" }
