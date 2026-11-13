[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [ValidateRange(1,4)][int]$MaxParallelActions=2)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[Guid]::NewGuid().ToString('N')
$root=Join-Path $workspace ('Saved/BuildChecks/RidingLobbyCommand-20261114/'+$id)
$tempRoot=Join-Path $workspace 'Saved/RL'
$temp=Join-Path $tempRoot $id.Substring(0,8)
$project=Join-Path $temp 'Project'
$reservation='riding-lobby-command-owner-20261114'
$process=$null;$failure=$null;$ownsTemp=$false
$proof=[ordered]@{Passed=$false;Head='';Inputs=@();Tests=@();Cleanup=$null;Scope='Published full Editor source plus explicit owned overlays. Actual LocalPlayer/subsystem lifetime and native transition entrypoints, fake command context/transport. No actual authenticated lobby command/asset refresh/write UI/PG/Steam.'}
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Save-Proof { [IO.File]::WriteAllText((Join-Path $root 'Result.json'),($proof|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false)) }
function Assert-Reservation {
    $rows=@(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md')|Where-Object{$_-match('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')})
    if($rows.Count-ne1){throw 'Missing riding lobby command reservation'}
}
function Assert-NativeResults($Result, [string]$LogPath) {
    $expected=@('ScreenLifetime','RestoreBeforeScreen','NativeFlowGuards','FreshReadBarrier')|ForEach-Object{'ProjectRPG.Riding.LobbyCommand.'+$_}
    if($Result.Tests.Count-ne4-or@($expected|Where-Object{$_-notin$Result.Tests.fullTestPath}).Count-or@($Result.Tests|Where-Object{$_.state-ne'Success'-or$_.errors}).Count){throw 'Missing/failing riding lobby cases'}
    # These pre-existing example/content diagnostics are unrelated to the command tests.
    # Exact message families and maximum counts keep all other automation warnings fatal.
    $patterns=@(
        @{Maximum=3;Pattern='^LogBlueprint: Node .*/Game/System/Experiences/Examples/BP_ExperienceReadyComponentExample\..* missing NodeGuid, this can cause deterministic cooking issues please resave package\. \[log\] $'},
        @{Maximum=7;Pattern='^LogBlueprint: .*Content[\\/]RuinedCrypt[\\/]Demo[\\/]EpicContent[\\/]ThirdPersonBP[\\/]Blueprints[\\/]ThirdPersonCharacter_SF\.uasset: .*Input(Action|Axis).*(ResetVR|LookUpRate|TurnRate|MoveRight|MoveForward|Turn|LookUp).* \[log\] $'},
        @{Maximum=2;Pattern='^LogBlueprint: FMemberReference::ResolveMember \((addfire|Chunks_numbers)\) bSelfContext == true, but no scope supplied! \[log\] $'},
        @{Maximum=1;Pattern='^LogUObjectGlobals: .*Object /Game/Characters/Mannequins/Rigs/CR_Mannequin_BasicFootIK\.CR_Mannequin_BasicFootIK.* \[log\] $'},
        @{Maximum=1;Pattern='^LogLinker: \[AssetLog\] .*Content[\\/]M5VFXVOL2[\\/]BluePrints[\\/]Niagara_BP[\\/]Nbuilding_DM_Blueprint\.uasset: VerifyImport: Failed to find script package for import object .Package /Script/ApexDestruction. \[log\] $'},
        @{Maximum=92;Pattern='^LogLinker: \[AssetLog\] .*Content[\\/]M5VFXVOL2[\\/]BluePrints[\\/]Niagara_BP[\\/]Nbuilding_DM_Blueprint\.uasset: CreateExport: Failed to load Class /Script/ApexDestruction\.DestructibleActor as Parent for BlueprintGeneratedClass /Game/M5VFXVOL2/BluePrints/Niagara_BP/Nbuilding_DM_Blueprint\.Nbuilding_DM_Blueprint_C - both will fail to load \[log\] $'},
        @{Maximum=1;Pattern='^LogLinker: Unable to load DestructibleComponent0 with outer Nbuilding_DM_Blueprint_C /Game/M5VFXVOL2/BluePrints/Niagara_BP/Nbuilding_DM_Blueprint\.Default__Nbuilding_DM_Blueprint_C because its class \(DestructibleComponent\) does not exist \[log\] $'},
        @{Maximum=1;Pattern='^LogLinker: Unable to load Default__Nbuilding_DM_Blueprint_C with outer Package /Game/M5VFXVOL2/BluePrints/Niagara_BP/Nbuilding_DM_Blueprint because its class \(Nbuilding_DM_Blueprint_C\) does not exist \[log\] $'},
        @{Maximum=31;Pattern='^LoadErrors: CreateExport: .* (BlueprintGeneratedClass|SimpleConstructionScript) /Game/M5VFXVOL2/BluePrints/Niagara_BP/Nbuilding_DM_Blueprint\.Nbuilding_DM_Blueprint_C.*Outer.* \[log\] $'},
        @{Maximum=4;Pattern='^LogEnhancedInput: UEnhancedInputLocalPlayerSubsystem for Local Player .LocalPlayer(_[0-9]+)?. does not have a valid PlayerInput object\. Failed to load user settings\. \[log\] $'}
    )
    $warnings=@(Get-Content -LiteralPath $LogPath -Encoding UTF8|Where-Object{$_-match'LogAutomationController: Warning: '}|ForEach-Object{($_-split'LogAutomationController: Warning: ',2)[1]})
    foreach($message in $warnings){if(@($patterns|Where-Object{$message-match$_.Pattern}).Count-ne1){throw ('Unexpected native warning: '+$message)}}
    foreach($p in $patterns){$n=@($warnings|Where-Object{$_-match$p.Pattern}).Count;if($n-ne0-and$n-ne$p.Maximum){throw 'Known diagnostic count changed'}}
    if(($Result.Tests|Measure-Object warnings -Sum).Sum-ne$warnings.Count){throw 'Native warning evidence mismatch'}
    $Result.ExpectedFixtureWarnings=@($warnings|Where-Object{$_-like'LogEnhancedInput:*'}).Count
    $Result.ExpectedContentWarnings=$warnings.Count-$Result.ExpectedFixtureWarnings
}
try {
    Assert-Reservation
    $ancestor=$temp
    while($ancestor){if((Test-Path -LiteralPath $ancestor)-and((Get-Item -LiteralPath $ancestor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor rejected'};$ancestor=[IO.Path]::GetDirectoryName($ancestor)}
    if(Test-Path -LiteralPath $temp){throw 'Owned temporary directory already exists'}
    $null=[IO.Directory]::CreateDirectory($root);$null=[IO.Directory]::CreateDirectory($project);$ownsTemp=$true
    $proof.Head=(git -C $workspace rev-parse HEAD).Trim()
    $proof.RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash
    $paths=[Collections.Generic.List[string]]::new()
    foreach($p in @('Source','Config','Project_RPG.uproject')){$paths.Add($p)}
    $pluginFiles=@(git -C $workspace ls-tree -r --name-only $proof.Head -- Plugins)
    $descriptors=@($pluginFiles|Where-Object{$_-like'*.uplugin'})
    foreach($descriptor in $descriptors){
        $paths.Add($descriptor);$dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        foreach($name in @('Source','Config','Resources','Shaders')){$child=$dir+'/'+$name;if(@($pluginFiles|Where-Object{$_.StartsWith($child+'/')}).Count){$paths.Add($child)}}
    }
    $archive=Join-Path $temp 'Inputs.tar'
    & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $paths.ToArray()
    if($LASTEXITCODE-ne0){throw 'Archive failed'}
    & tar -xf $archive -C $project
    if($LASTEXITCODE-ne0){throw 'Extraction failed'}
    $overlay=@('Source/Project_RPG/Public/Riding/RPGRidingCollectionSubsystem.h','Source/Project_RPG/Private/Riding/RPGRidingCollectionSubsystem.cpp',
        'Source/Project_RPG/Private/UI/Lobby/RPGOnlineLobbyWidget.cpp','Source/Project_RPG/Private/Online/RPGOnlineLobbySubsystem.cpp',
        'Source/Project_RPG/Public/Online/RPGDungeonSessionSubsystem.h','Source/Project_RPG/Private/Online/RPGDungeonSessionSubsystem.cpp',
        'Source/Project_RPG/Private/Tests/RPGRidingLobbyCommandTests.cpp')
    foreach($relative in $overlay){
        $source=Join-Path $workspace $relative;$destination=Join-Path $project $relative;$hash=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination));Copy-Item -LiteralPath $source -Destination $destination
        if((Get-FileHash -LiteralPath $destination).Hash-ne$hash-or(Get-FileHash -LiteralPath $source).Hash-ne$hash){throw 'Input changed during capture'}
        $proof.Inputs+=@{Path=$relative;SHA256=$hash}
    }
    $null=New-Item -ItemType Junction -Path (Join-Path $project 'Content') -Target (Join-Path $workspace 'Content')
    foreach($descriptor in $descriptors){$dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'));$content=Join-Path $workspace ($dir+'/Content');if(Test-Path -LiteralPath $content){$null=New-Item -ItemType Junction -Path (Join-Path $project ($dir+'/Content')) -Target $content}}
    $config=Join-Path $project 'Saved/UnrealBuildTool/BuildConfiguration.xml'
    $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $config))
    [IO.File]::WriteAllText($config,'<?xml version="1.0" encoding="utf-8"?><Configuration xmlns="https://www.unrealengine.com/BuildConfiguration"><ParallelExecutor><bStopCompilationAfterErrors>true</bStopCompilationAfterErrors></ParallelExecutor></Configuration>',[Text.UTF8Encoding]::new($false))
    if(!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt'))){throw 'Installed engine required'}
    $uproject=Join-Path $project 'Project_RPG.uproject'
    $build=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $arguments=@($ubt,'Project_RPGEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),'-NoHotReloadFromIDE',('-MaxParallelActions='+$MaxParallelActions),('-log='+(Join-Path $root 'Build.log')))
    $proof.BuildCommand=@($build)+$arguments;Save-Proof
    Write-Output ('Building full isolated riding lobby owner; proof='+$root)
    & $build @arguments *> (Join-Path $root 'BuildConsole.log')
    $proof.BuildExit=$LASTEXITCODE;if($LASTEXITCODE-ne0){throw 'Independent full Editor build failed'}
    $module=Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll';$proof.ModuleSHA256=(Get-FileHash -LiteralPath $module).Hash
    $report=Join-Path $temp 'Report';$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $arguments=@(('"'+$uproject+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG.Riding.LobbyCommand."','-TestExit="Automation Test Queue Empty"',
        '-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',
        ('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),
        ('-abslog="'+(Join-Path $root 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'),('-RidingLobbyRoot="'+(Join-Path $temp 'Journal')+'"'))
    $proof.NativeCommand=@($editor)+$arguments;Save-Proof
    $process=Start-Process $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru;$null=$process.Handle
    $deadline=[DateTime]::UtcNow.AddSeconds(240)
    while(!$process.HasExited){if([DateTime]::UtcNow-ge$deadline){throw 'Owned automation timed out'};$null=$process.WaitForExit(200)}
    $process.WaitForExit();$proof.NativeExit=$process.ExitCode
    if($process.ExitCode-ne0){throw 'Native automation failed'}
    $data=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $proof.Tests=@($data.tests|Select-Object fullTestPath,state,errors,warnings)
    Assert-NativeResults $proof (Join-Path $root 'Native.log')
    foreach($entry in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash-ne$entry.SHA256){throw 'Validated input drift'}}
    $proof.Passed=$true
} catch {$failure=$_.Exception.Message}
finally {
    $released=$true
    if($process){try{if(!$process.HasExited){$process.Kill()};if(!$process.WaitForExit(10000)){throw 'Owned process still active'}}catch{$released=$false;$failure=$_.Exception.Message}}
    try{Assert-Reservation}catch{$released=$false;$failure=$_.Exception.Message}
    if($released-and$ownsTemp-and(Test-Path -LiteralPath $temp)){try{$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}catch{$failure=$_.Exception.Message}}
    $proof.Failure=$failure;$proof.CleanupComplete=!(Test-Path -LiteralPath $temp);$proof.Passed=$proof.Passed-and!$failure-and$proof.CleanupComplete;$proof.HistoricalHashesOnly=$true
    if(Test-Path -LiteralPath $root){Save-Proof};Write-Output ('Passed='+$proof.Passed+'; proof='+$root)
}
if(!$proof.Passed){throw $failure}
