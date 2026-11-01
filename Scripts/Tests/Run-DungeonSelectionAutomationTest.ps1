[CmdletBinding()]
param([string]$EngineRoot='D:\UE_5.8',[ValidateRange(60,600)][int]$TimeoutSeconds=600,
    [switch]$RegisteredAssetOnly,[switch]$ServerContentOnly,[string]$ExactTest='')
$ErrorActionPreference='Stop'
if($RegisteredAssetOnly -and $ServerContentOnly) { throw 'Choose one focused dungeon definition mode.' }
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$module=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-Project_RPG.dll'
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
foreach($file in @($module,$editor)) { if(-not(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing prerequisite: $file" } }
$files=@('Public/Online/RPGDungeonDefinition.h','Private/Online/RPGDungeonDefinition.cpp',
    'Public/UI/Lobby/RPGOnlineLobbyWidget.h','Private/UI/Lobby/RPGOnlineLobbyWidget.cpp',
    'Private/UI/Lobby/RPGOnlineLobbyDungeonSelection.cpp','Private/Tests/RPGDungeonSelectionTests.cpp',
    'Private/Tests/RPGOnlineLobbyTests.cpp','Public/Online/RPGDungeonSessionSubsystem.h',
    'Private/Online/RPGDungeonSessionSubsystem.cpp','Private/Online/RPGOnlineLobbySubsystem.cpp',
    'Public/Input/RPGAdditionalInputProvider.h','Private/Input/RPGAdditionalInputProvider.cpp') |
    ForEach-Object { Join-Path $projectRoot "Source/Project_RPG/$_" }
if($ServerContentOnly) {
    $files=@('Public/Online/RPGDungeonDefinition.h','Private/Online/RPGDungeonDefinition.cpp',
        'Public/Online/RPGDungeonServerDefinition.h','Private/Online/RPGDungeonServerDefinition.cpp',
        'Public/GameMode/RPGGameModeBase.h','Private/GameMode/RPGGameModeBase.cpp',
        'Public/Economy/RPGDungeonRewardDefinition.h','Private/Economy/RPGDungeonRewardDefinition.cpp',
        'Private/Tests/RPGDungeonServerDefinitionTests.cpp') | ForEach-Object { Join-Path $projectRoot "Source/Project_RPG/$_" }
    $files+=@((Join-Path $projectRoot 'Content/System/Dungeons/DA_DungeonServer_RaidSwordMan.uasset'),
        (Join-Path $projectRoot 'Content/Blueprints/GameMode/BP_BossBattleGameMode.uasset'),
        (Join-Path $projectRoot 'Scripts/Editor/CreateDungeonServerDefinitionAssets.py'))
}
$files+=@($PSCommandPath,(Join-Path $projectRoot 'Config/DefaultGame.ini'),
    (Join-Path $projectRoot 'Content/System/Dungeons/DA_Dungeon_RaidSwordMan.uasset'),
    (Join-Path $projectRoot 'Scripts/Editor/CreateDungeonDefinitionAssets.py'))
$hashes=@($files | ForEach-Object { [ordered]@{path=$_;SHA256=(Get-FileHash -LiteralPath $_).Hash} })
$moduleHash=(Get-FileHash -LiteralPath $module).Hash
foreach($file in $files | Where-Object { $_ -match '\.(cpp|h)$' }) {
    if((Get-Item -LiteralPath $file).LastWriteTimeUtc -gt (Get-Item -LiteralPath $module).LastWriteTimeUtc) { throw 'Build the current Editor module first.' }
}
$testNames=@('DungeonSelection.Definition','DungeonSelection.Catalog','DungeonSelection.Widget','DungeonSelection.BlueprintContract',
    'DungeonSelection.RegisteredAsset',
    'DungeonLobbyPolicy','DungeonLobbyCharacterName','DungeonLobbyWidget',
    'DungeonLobbyDifficulty.BlueprintContract','DungeonLobbyDifficulty.SessionDisplay')
if ($RegisteredAssetOnly) { $testNames=@('DungeonSelection.RegisteredAsset','DungeonLobbyWidget','DungeonLobbyDifficulty.SessionDisplay') }
if ($ServerContentOnly) { $testNames=@('DungeonServerContent.Definition','DungeonServerContent.Catalog','DungeonServerContent.GameMode','DungeonServerContent.AuthoredDefault') }
$tests=@($testNames | ForEach-Object { 'ProjectRPG.Online.'+$_ })
$testFilter=if($RegisteredAssetOnly) { $tests -join '+' } else { 'ProjectRPG.Online.DungeonSelection+ProjectRPG.Online.DungeonLobby' }
if($ServerContentOnly) { $testFilter='ProjectRPG.Online.DungeonServerContent' }
if(-not [string]::IsNullOrEmpty($ExactTest)) {
    if($ExactTest -cnotin $tests) { throw 'ExactTest must be an exact test name from the selected focused mode.' }
    $tests=@($ExactTest)
    $testFilter=$ExactTest
}
$runRoot=Join-Path $projectRoot ('Saved/Tests/DungeonSelection/'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$log=Join-Path $runRoot 'automation.log'
$taskSdkBefore=$env:UE_SKIP_UBT_SDK_SETUP
$process=$null; $passed=$false; $failure=$null
try {
    $env:UE_SKIP_UBT_SDK_SETUP='1'
    $arguments=@(('"'+(Join-Path $projectRoot 'Project_RPG.uproject')+'"'),'/Engine/Maps/Entry',
        ('-ExecCmds="Automation RunTests '+$testFilter+'"'),
        '-TestExit="Automation Test Queue Empty"','-unattended','-nop4','-nosplash','-nosound','-nullrhi','-nosteam',
        '-DisablePython','-NoLiveCoding','-DDC=(EnginePak,Local)',('-UserDir="'+(Join-Path $runRoot 'User')+'"'),
        '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
        ('-abslog="'+$log+'"'))
    $process=Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
    $null=$process.Handle
    $deadline=[DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while(-not $process.HasExited) {
        if([DateTimeOffset]::UtcNow -ge $deadline) { throw 'Focused dungeon selection automation timed out.' }
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    $process.WaitForExit()
    if($process.ExitCode -ne 0) { throw "Owned editor exited $($process.ExitCode)." }
    $body=[string](Get-Content -LiteralPath $log -Raw -Encoding UTF8)
    $results=[regex]::Matches($body,'Test Completed\. Result=\{([^}]+)\} Name=\{[^}]+\} Path=\{(ProjectRPG\.[^}]+)\}')
    if($body -match 'Result=\{Fail\}|Fatal error:|Assertion failed:|Ensure condition failed:' -or $results.Count -ne $tests.Count) { throw 'Unexpected focused selection automation results.' }
    foreach($test in $tests) {
        if(@($results | Where-Object { $_.Groups[1].Value -ceq 'Success' -and $_.Groups[2].Value -ceq $test }).Count -ne 1) { throw "Missing successful result: $test" }
    }
    foreach($entry in $hashes) { if((Get-FileHash -LiteralPath $entry.path).Hash -cne $entry.SHA256) { throw "Selected source changed during native run: $($entry.path)" } }
    if((Get-FileHash -LiteralPath $module).Hash -cne $moduleHash) { throw 'Shared DLL changed during native run.' }
    $passed=$true
} catch { $failure=$_.Exception.Message }
finally {
    if($process) {
        if(-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
    $env:UE_SKIP_UBT_SDK_SETUP=$taskSdkBefore
    [ordered]@{passed=$passed;failure=$failure;tests=$tests;moduleSHA256=$moduleHash;sourceHashes=$hashes;
        log=$log;closedLogSHA256=$(if(Test-Path -LiteralPath $log){(Get-FileHash -LiteralPath $log).Hash}else{$null});
        scope=$(if($ServerContentOnly){'Native server content definition/catalog/GameMode pinning and saved default BP binding; not real admission/HTTP/DB settlement, allocation, Steam or Shipping'}else{'Native definitions and actual widget dispatchers including saved default definition/Config load and configured cook rule; not real authenticated create HTTP, map assignment, packaged cooking, Steam, GPU or Shipping'})} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'Verification.proof.json') -Encoding UTF8
}
if(-not $passed) { throw "$failure Inspect $runRoot" }
Write-Host "PASS: $($tests.Count) focused dungeon selection/lobby native tests. $runRoot"
