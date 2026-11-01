# Explicit editor-build profile launch. Does not change project defaults or saved characters.
[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:\UE_5.8',
    [string]$ProjectRoot = '',
    [string]$Profile = '/Game/Blueprints/GameData/PlayerClass/Development/DA_GreatSword_Development.DA_GreatSword_Development',
    [string]$Map = '/Game/Maps/testmap',
    [switch]$PrintCommand
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Join-Path $PSScriptRoot '..\..' }
$classDevelopmentRoot = [IO.Path]::GetFullPath($ProjectRoot)
$classDevelopmentProject = Join-Path $classDevelopmentRoot 'Project_RPG.uproject'
$classDevelopmentEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if ($Profile -notmatch '^/Game/[A-Za-z0-9_/]+\.[A-Za-z0-9_]+$') { throw 'Profile must be a /Game asset object path.' }
if ($Map -notmatch '^/Game/[A-Za-z0-9_/]+$') { throw 'Map must be a /Game package path.' }
$classProfilePackage = ($Profile -split '\.')[0].Substring('/Game/'.Length)
$classDevelopmentProfileFile = Join-Path $classDevelopmentRoot ('Content\' + $classProfilePackage + '.uasset')
foreach ($required in @($classDevelopmentProject, $classDevelopmentEditor)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing file: $required" }
}
if (-not $PrintCommand -and -not (Test-Path -LiteralPath $classDevelopmentProfileFile -PathType Leaf)) {
    throw 'Create the development class assets after a normal Editor build first (CreateGreatSwordClassDevelopment.py).'
}
$classDevelopmentArguments = @(
    $classDevelopmentProject, $Map, '-game', '-windowed', '-ResX=1280', '-ResY=720',
    '-nop4', '-nosplash', '-nosteam', '-NoLiveCoding', '-multiprocess',
    ('-RPGDevelopmentClassProfile=' + $Profile)
)
if ($PrintCommand) {
    Write-Output ('& "' + $classDevelopmentEditor + '" ' + (($classDevelopmentArguments | ForEach-Object { '"' + $_ + '"' }) -join ' '))
    return
}
& $classDevelopmentEditor @classDevelopmentArguments
if ($LASTEXITCODE -ne 0) { throw "Development game exited with code $LASTEXITCODE" }
