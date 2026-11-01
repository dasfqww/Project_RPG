[CmdletBinding()]
param([string]$EngineRoot='D:\UE_5.8')
$ErrorActionPreference='Stop'
$workspace=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$launcher=Join-Path $workspace 'Scripts/Start-TrainingDevelopment.ps1'
$root=Join-Path $workspace 'Saved/BuildChecks/TrainingDevelopmentLauncher-20261031'
$temp=Join-Path $root 'Temp'
. (Join-Path $PSScriptRoot 'TrainingGuideArtifacts.ps1')
$result=[ordered]@{passed=$false;failure=$null;cleanup=$null;groups=@()}
try{
    if(Test-Path -LiteralPath $temp){throw 'Own fixture Temp already exists'}
    $null=New-Item -ItemType Directory -Path $temp -Force
    foreach($relative in @('Scripts/Start-TrainingDevelopment.ps1','Backend/Tools/Start-LocalDungeonServer.ps1','Backend/Tools/Test-DungeonContentAllocator.ps1','Scripts/Tests/TrainingGuideArtifacts.ps1')){
        $tokens=$null;$errors=$null
        $null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $workspace $relative),[ref]$tokens,[ref]$errors)
        if($errors.Count){throw ('Parser errors: '+$relative)}
    }
    $result.groups+='Parsers'
    $rejected=$false
    try{& $launcher -BackendPort 1}catch [Management.Automation.ParameterBindingException]{$rejected=$true}
    if(!$rejected){throw 'Invalid port was not rejected before startup'}
    $result.groups+='ArgumentPreflight'
    # Existing mocked allocator tests check selection, credential boundaries,
    # short UserDir forwarding and rejection before assignment. No UE is started.
    & {
        Set-StrictMode -Off
        & (Join-Path $workspace 'Backend/Tools/Test-DungeonContentAllocator.ps1') -OutputDirectory (Join-Path $temp 'Allocator') `
            -EditorExecutable (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ProjectFile (Join-Path $workspace 'Project_RPG.uproject')
    } *> (Join-Path $root 'AllocatorMocks.log')
    $result.groups+='AllocatorMocks'
    $outside=Join-Path $workspace 'Config'
    $denied=$false
    try{$null=Remove-RPGTrainingGuideTemporaryTree -Path $outside -OwnedParent $root}catch{$denied=$true}
    if(!$denied){throw 'Cleanup allowed an original directory'}
    $fixture=Join-Path $temp 'LinkFixture';$target=Join-Path $temp 'OriginalSentinel'
    $null=New-Item -ItemType Directory -Path $fixture,$target
    $sentinel=Join-Path $target 'keep.txt';[IO.File]::WriteAllText($sentinel,'preserve')
    $null=New-Item -ItemType Junction -Path (Join-Path $fixture 'Content') -Target $target
    $removed=Remove-RPGTrainingGuideTemporaryTree -Path $fixture -OwnedParent $root
    if(!(Test-Path -LiteralPath $sentinel) -or $removed.unlinked -ne 1){throw 'Cleanup followed the content link'}
    $result.groups+='CleanupBoundaryAndJunction'
    $result.passed=$true
}catch{$result.failure=$_.Exception.Message}
finally{
    try{$result.cleanup=Remove-RPGTrainingGuideTemporaryTree -Path $temp -OwnedParent $root}catch{$result.passed=$false;$result.failure=([string]$result.failure+'; cleanup: '+$_.Exception.Message).Trim(';',' ')}
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'ScriptTests.json') -Encoding UTF8
}
$result | ConvertTo-Json -Depth 6
if(!$result.passed){throw $result.failure}
