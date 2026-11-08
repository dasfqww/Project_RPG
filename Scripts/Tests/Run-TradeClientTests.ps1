[CmdletBinding()]
param([string]$EngineRoot = 'D:/UE_5.8')
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id = [Guid]::NewGuid().ToString('N')
$proofRoot = Join-Path $workspace ('Saved/BuildChecks/TradeClient-20261108/' + $id)
$tempRoot = Join-Path $workspace 'Saved/TC'
$temp = Join-Path $tempRoot $id.Substring(0,8)
$project = Join-Path $temp 'Project'
$proof = [ordered]@{Passed=$false; Scope='Actual trading client sources in isolated UE native module; no HTTP/UI/full game integration'; Inputs=@(); Tests=@(); Cleanup=$null; Failure=$null}
$process = $null
$ownsTemp = $false
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path, [string]$Text) {
    $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Save-Proof { Write-Utf8 (Join-Path $proofRoot 'Result.json') ($proof | ConvertTo-Json -Depth 10) }
function Assert-Reservation {
    $rows = @(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') | Where-Object {$_ -match '^\|[^|]* / trading-client-20261108 \|'})
    if ($rows.Count -ne 1) { throw 'Trading client validation reservation missing' }
}
try {
    Assert-Reservation
    if (Test-Path -LiteralPath $temp) { throw 'Owned temporary path already exists' }
    $null = [IO.Directory]::CreateDirectory((Join-Path $project 'Content'))
    $ownsTemp = $true
    $proof.Head = (git -C $workspace rev-parse HEAD).Trim()
    $files = @('Source/Project_RPG/Public/Trading/RPGTradeClient.h','Source/Project_RPG/Private/Trading/RPGTradeClient.cpp','Source/Project_RPG/Private/Tests/RPGTradeClientTests.cpp','Source/Project_RPG/Private/Trading/RPGTradeJson.h')
    foreach ($relative in $files) {
        $source = Join-Path $workspace $relative
        $destination = Join-Path $project $relative
        $hash = (Get-FileHash -LiteralPath $source).Hash
        $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $destination).Hash -ne $hash -or (Get-FileHash -LiteralPath $source).Hash -ne $hash) { throw 'Source changed during capture' }
        $proof.Inputs += @{Path=$relative; SHA256=$hash}
    }
    $proof.RunnerSHA256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash
    Write-Utf8 (Join-Path $project 'TradeClient.uproject') @'
{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}
'@
    Write-Utf8 (Join-Path $project 'Source/TradeClientEditor.Target.cs') @'
using UnrealBuildTool;
public class TradeClientEditorTarget : TargetRules {
    public TradeClientEditorTarget(TargetInfo Target) : base(Target) {
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
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "Json"});
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Private/TradeClientModule.cpp') @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Project_RPG, "TradeClient");
'@
    $uproject = Join-Path $project 'TradeClient.uproject'
    if (!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt'))) { throw 'Installed engine required' }
    $build = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt = Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $buildArgs = @($ubt,'TradeClientEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),('-UBATraceOutputFile='+(Join-Path $temp 'Build.uba')),'-NoHotReloadFromIDE','-MaxParallelActions=1',('-log='+(Join-Path $proofRoot 'Build.log')))
    $proof.BuildCommand = @($build) + $buildArgs
    Save-Proof
    Write-Output ('Building isolated trading client; proof: '+$proofRoot)
    & $build @buildArgs *> (Join-Path $proofRoot 'BuildConsole.log')
    $proof.BuildExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Isolated C++ build failed' }
    $proof.ModuleSHA256 = (Get-FileHash -LiteralPath (Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll')).Hash
    $queryArgs = @($ubt,'-Mode=QueryTargets',('-Project='+$uproject),('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')),'-IncludeAllTargets','-DontIncludeParentAssembly','-DontIncludeProgramTargets','-NoMutex','-SkipRulesCompile','-NoLog')
    & $build @queryArgs *> (Join-Path $proofRoot 'Query.log')
    $proof.QueryExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Isolated target query failed' }
    $report = Join-Path $temp 'Report'
    $editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $editorArgs = @(('"'+$uproject+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG.Trading.Client"','-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'))
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
    if ($process.ExitCode -ne 0) { throw 'Native process failed' }
    $result = Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $proof.Tests = @($result.tests | Select-Object fullTestPath,state,errors,warnings)
    $expected = @('Wire','Commands','UncertainRetry','Ordering','Lifetime','Receipts') | ForEach-Object {'ProjectRPG.Trading.Client.'+$_}
    if ((Compare-Object $expected @($proof.Tests.fullTestPath)) -or @($proof.Tests | Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0 -or $_.warnings -ne 0}).Count) { throw 'Missing or failing native cases' }
    foreach ($inputFile in $proof.Inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash -ne $inputFile.SHA256) { throw 'Original source changed during validation' }
    }
    if ((Get-FileHash -LiteralPath $PSCommandPath).Hash -ne $proof.RunnerSHA256) { throw 'Runner changed during validation' }
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
        if ($ownsTemp -and (Test-Path -LiteralPath $temp)) { $proof.Cleanup = Remove-GuardianProbeArtifact $temp $tempRoot }
    } catch { $proof.Failure=$_.Exception.Message }
    $proof.CleanupComplete = !(Test-Path -LiteralPath $temp)
    $proof.HistoricalHashesOnly = $true
    $proof.Passed = $proof.Passed -and !$proof.Failure -and $proof.CleanupComplete
    Save-Proof
    Write-Output ('Passed='+$proof.Passed+'; proof='+$proofRoot)
}
if (!$proof.Passed) { throw $proof.Failure }
