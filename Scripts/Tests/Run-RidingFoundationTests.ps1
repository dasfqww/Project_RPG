[CmdletBinding()]
param([string]$EngineRoot = 'D:/UE_5.8', [switch]$IncludeSession, [switch]$IncludeActors, [switch]$IncludePresentation)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id = [Guid]::NewGuid().ToString('N')
if ($IncludePresentation) { $IncludeActors = $true }
if ($IncludeActors) { $IncludeSession = $true }
$suite = if ($IncludePresentation) { 'RidingPresentation' } elseif ($IncludeActors) { 'RidingActors' } elseif ($IncludeSession) { 'RidingSession' } else { 'RidingFoundation' }
$reservation = if ($IncludePresentation) { 'riding-presentation-20261108' } elseif ($IncludeActors) { 'riding-actors-20261106' } elseif ($IncludeSession) { 'riding-session-20261106' } else { 'riding-foundation-20261106' }
$testFilter = if ($IncludeSession) { 'ProjectRPG.Riding' } else { 'ProjectRPG.Riding.Foundation' }
$proofDate = if ($IncludePresentation) { '20261108' } else { '20261106' }
$proofRoot = Join-Path $workspace ('Saved/BuildChecks/' + $suite + '-' + $proofDate + '/' + $id)
$tempRoot = Join-Path $workspace 'Saved/RF'
$temp = Join-Path $tempRoot $id.Substring(0,8)
$project = Join-Path $temp 'Project'
$proof = [ordered]@{Passed=$false; Scope=('Isolated '+$suite+' module, not full game integration'); Head=''; Inputs=@(); Tests=@(); Cleanup=$null; Failure=$null}
$process = $null
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path, [string]$Text) {
    $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Save-Proof { Write-Utf8 (Join-Path $proofRoot 'Result.json') ($proof | ConvertTo-Json -Depth 10) }
function Assert-Reservation {
    $rows = @(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') | Where-Object {$_ -match ('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')})
    if ($rows.Count -ne 1) { throw 'Riding validation reservation missing' }
}
try {
    Assert-Reservation
    $null = [IO.Directory]::CreateDirectory($project)
    if ($IncludePresentation) {
        $proof.AssetInputs = @()
        foreach ($asset in @('Content/GreatswordComboPack/Demo/Mannequin/Character/Mesh/SK_Mannequin.uasset','Content/GreatswordComboPack/Demo/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton.uasset')) {
            $proof.AssetInputs += @{Path=$asset;SHA256=(Get-FileHash -LiteralPath (Join-Path $workspace $asset)).Hash}
        }
        $null = New-Item -ItemType Junction -Path (Join-Path $project 'Content') -Target (Join-Path $workspace 'Content')
    } else { $null = [IO.Directory]::CreateDirectory((Join-Path $project 'Content')) }
    $proof.Head = (git -C $workspace rev-parse HEAD).Trim()
    $files = @('Source/Project_RPG/Private/Tests/RPGRidingFoundationTests.cpp')
    foreach ($name in @('RPGRidingTypes','RPGRidingPolicy','RPGMountDefinition','RPGRidingStatePolicy')) {
        $files += 'Source/Project_RPG/Public/Riding/' + $name + '.h'
        $files += 'Source/Project_RPG/Private/Riding/' + $name + '.cpp'
    }
    if ($IncludeSession) {
        $files += 'Source/Project_RPG/Private/Tests/RPGRidingSessionTests.cpp'
        $files += 'Source/Project_RPG/Public/Security/RPGServerRequestBudget.h'
        foreach ($name in @('RPGRidingRequestLedger','RPGRidingSession')) {
            $files += 'Source/Project_RPG/Private/Riding/' + $name + '.h'
            $files += 'Source/Project_RPG/Private/Riding/' + $name + '.cpp'
        }
    }
    if ($IncludeActors) {
        $files += @('Source/Project_RPG/Private/Tests/RPGRidingActorTests.h','Source/Project_RPG/Private/Tests/RPGRidingActorTests.cpp','Source/Project_RPG/Public/Component/PawnExtensionComponentBase.h','Source/Project_RPG/Private/Component/PawnExtensionComponentBase.cpp')
        foreach ($name in @('RPGRidingSessionComponent','RPGRidingComponent')) {
            $files += 'Source/Project_RPG/Public/Riding/' + $name + '.h'
            $files += 'Source/Project_RPG/Private/Riding/' + $name + '.cpp'
        }
    }
    if ($IncludePresentation) {
        $files += @('Source/Project_RPG/Public/Riding/RPGRidingPresentationComponent.h','Source/Project_RPG/Private/Riding/RPGRidingPresentationComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingPresentationTests.cpp')
    }
    foreach ($relative in $files) {
        $source = Join-Path $workspace $relative
        $destination = Join-Path $project $relative
        $hash = (Get-FileHash -LiteralPath $source).Hash
        $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $destination).Hash -ne $hash -or (Get-FileHash -LiteralPath $source).Hash -ne $hash) { throw 'Source changed during capture' }
        $proof.Inputs += @{Path=$relative; SHA256=$hash}
    }
    # Compile the actual feature files, with their real module/export names and UHT.
    # No shared project binaries, incomplete peer sources, content, or plugins are copied.
    Write-Utf8 (Join-Path $project 'RidingFoundation.uproject') @'
{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Plugins":[{"Name":"GameplayAbilities","Enabled":true}],"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}
'@
    if ($IncludeActors) {
        $cuePath = if ($IncludePresentation) { '/Game/GreatswordComboPack' } else { '/Game/' }
        Write-Utf8 (Join-Path $project 'Config/DefaultGame.ini') ("[/Script/GameplayAbilities.AbilitySystemGlobals]`n+GameplayCueNotifyPaths="+$cuePath+"`n")
    }
    Write-Utf8 (Join-Path $project 'Source/RidingFoundationEditor.Target.cs') @'
using UnrealBuildTool;
public class RidingFoundationEditorTarget : TargetRules {
    public RidingFoundationEditorTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("Project_RPG");
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Project_RPG.Build.cs') @'
using UnrealBuildTool;
public class Project_RPG : ModuleRules {
    public Project_RPG(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "NetCore", "GameplayTags", "GameplayAbilities", "GameplayTasks"});
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Private/RidingFoundationModule.cpp') @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Project_RPG, "RidingFoundation");
'@
    $uproject = Join-Path $project 'RidingFoundation.uproject'
    if (!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt'))) { throw 'This isolated runner requires an installed engine' }
    $build = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt = Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    # Unique minimal project/rules/intermediates/logs; consume installed engine outputs only.
    # Direct prebuilt UBT avoids Build.bat rebuilding shared tools or holding its global script lock.
    $buildArgs = @($ubt,'RidingFoundationEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),('-UBATraceOutputFile='+(Join-Path $temp 'Build.uba')),'-NoHotReloadFromIDE','-MaxParallelActions=1',('-log='+(Join-Path $proofRoot 'Build.log')))
    $proof.BuildCommand = @($build) + $buildArgs
    Save-Proof
    Write-Output ('Building isolated Riding foundation; proof: '+$proofRoot)
    & $build @buildArgs *> (Join-Path $proofRoot 'BuildConsole.log')
    $proof.BuildExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Isolated UHT/C++ build failed' }
    $proof.ModuleSHA256 = (Get-FileHash -LiteralPath (Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll')).Hash
    # Prevent editor startup from invoking the shared Build.bat QueryTargets path.
    $queryArgs = @($ubt,'-Mode=QueryTargets',('-Project='+$uproject),('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')),'-IncludeAllTargets','-DontIncludeParentAssembly','-DontIncludeProgramTargets','-NoMutex','-SkipRulesCompile','-NoLog')
    $proof.QueryCommand = @($build) + $queryArgs
    & $build @queryArgs *> (Join-Path $proofRoot 'Query.log')
    $proof.QueryExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Isolated target query failed' }
    $report = Join-Path $temp 'Report'
    $editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $editorArgs = @(('"'+$uproject+'"'),'/Engine/Maps/Entry',('-ExecCmds="Automation RunTests '+$testFilter+'"'),'-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'))
    $proof.NativeCommand = @($editor) + $editorArgs
    Save-Proof
    $process = Start-Process $editor -ArgumentList $editorArgs -WindowStyle Hidden -PassThru
    $null = $process.Handle
    $deadline = [DateTime]::UtcNow.AddSeconds(300)
    while (!$process.HasExited) {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Owned automation timed out' }
        $null = $process.WaitForExit(200)
    }
    $process.WaitForExit()
    $proof.NativeExit = $process.ExitCode
    if ($process.ExitCode -ne 0) { throw 'Native test process failed' }
    $result = Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $proof.Tests = @($result.tests | Select-Object fullTestPath,state,errors,warnings)
    $expected = @('Values','CanonicalState','AdmissionGuards','Transitions','Authoring') | ForEach-Object {'ProjectRPG.Riding.Foundation.'+$_}
    if ($IncludeSession) {
        $expected += @('LedgerReplay','LedgerBounds','Admission','SnapshotValidation','Requests','Lifetime') | ForEach-Object {'ProjectRPG.Riding.Session.'+$_}
    }
    if ($IncludeActors) {
        $expected += @('Transitions','Lifetime','Reentry','Results') | ForEach-Object {'ProjectRPG.Riding.Actors.'+$_}
    }
    if ($IncludePresentation) {
        $expected += @('Async','Lifecycle','Replacement','Failure','Reentry') | ForEach-Object {'ProjectRPG.Riding.Presentation.'+$_}
    }
    if ((Compare-Object $expected @($proof.Tests.fullTestPath)) -or @($proof.Tests | Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0 -or $_.warnings -ne 0}).Count) { throw 'Missing or failing native cases' }
    foreach ($inputFile in $proof.Inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash -ne $inputFile.SHA256) { throw 'Original source changed during validation' }
    }
    if ($IncludePresentation) {
        foreach ($asset in $proof.AssetInputs) {
            if ((Get-FileHash -LiteralPath (Join-Path $workspace $asset.Path)).Hash -ne $asset.SHA256) { throw 'Read-only source asset changed' }
        }
    }
    $proof.Passed = $true
} catch { $proof.Failure = $_.Exception.Message }
finally {
    $released = $true
    if ($process) {
        try {
            if (!$process.HasExited) { $process.Kill() }
            if (!$process.WaitForExit(10000)) { throw 'Owned test process still alive' }
        } catch { $released=$false; $proof.Failure=$_.Exception.Message }
    }
    try {
        Assert-Reservation
        if (!$released) { throw 'Cannot clean active runtime' }
        if (Test-Path -LiteralPath $temp) { $proof.Cleanup = Remove-GuardianProbeArtifact $temp $tempRoot }
    } catch { $proof.Failure=$_.Exception.Message }
    $proof.CleanupComplete = !(Test-Path -LiteralPath $temp)
    $proof.HistoricalHashesOnly = $true
    $proof.Passed = $proof.Passed -and !$proof.Failure -and $proof.CleanupComplete
    Save-Proof
    Write-Output ('Passed='+$proof.Passed+'; proof='+$proofRoot)
}
if (!$proof.Passed) { throw $proof.Failure }
