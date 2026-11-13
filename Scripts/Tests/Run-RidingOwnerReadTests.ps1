[CmdletBinding()]
param([string]$EngineRoot = 'D:/UE_5.8', [switch]$Screen)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id = [Guid]::NewGuid().ToString('N')
$proofRoot = Join-Path $workspace ('Saved/BuildChecks/RidingOwnerRead-20261114/' + $id)
$reservation = 'riding-owner-read-20261114'
if ($Screen) { $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingCollectionScreen-20261114/'+$id); $reservation='riding-collection-screen-20261114' }
$tempRoot = Join-Path $workspace 'Saved/RO'
$temp = Join-Path $tempRoot $id.Substring(0,8)
$project = Join-Path $temp 'Project'
$proof = [ordered]@{Passed=$false;Scope='Native Riding owner collection wire/reader and actual UE Player HTTP to private Memory Kestrel, no UI/PG/Steam/full game link';Inputs=@();Tests=@();Cleanup=$null;Failure=$null}
if ($Screen) { $proof.Scope='Native UMG read screen, filters/identity/authentication/close lifetimes, actual UMG click to reader to Player HTTP to Memory Kestrel; existing wire/reader/transport regressions. No concrete LocalPlayer/lobby/menu runtime, assets/GPU/PG/Steam/full game link' }
$process = $null
$hostProcess = $null
$ownsTemp = $false
$savedRun=[Environment]::GetEnvironmentVariable('PROJECT_RPG_RIDING_OWNER_RUN','Process')
$savedFixtures=[Environment]::GetEnvironmentVariable('PROJECT_RPG_RIDING_OWNER_FIXTURE','Process')
$savedSdkEnvironment=@{}
foreach($key in @('DOTNET_CLI_HOME','NUGET_PACKAGES','NUGET_HTTP_CACHE_PATH')){$savedSdkEnvironment[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path, [string]$Text) {
    $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Save-Proof { Write-Utf8 (Join-Path $proofRoot 'Result.json') ($proof | ConvertTo-Json -Depth 10) }
function Assert-Reservation {
    $rows = @(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') | Where-Object {$_ -match ('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')})
    if ($rows.Count -ne 1) { throw 'Riding owner validation reservation missing' }
}
try {
    Assert-Reservation
    $ancestor=$temp
    while($ancestor){if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor rejected'};$ancestor=[IO.Path]::GetDirectoryName($ancestor)}
    if (Test-Path -LiteralPath $temp) { throw 'Owned temporary path already exists' }
    $null = [IO.Directory]::CreateDirectory((Join-Path $project 'Content'))
    $ownsTemp = $true
    $proof.Head = (git -C $workspace rev-parse HEAD).Trim()
    $files = @('Source/Project_RPG/Public/Riding/RPGRidingTypes.h','Source/Project_RPG/Private/Riding/RPGRidingTypes.cpp','Source/Project_RPG/Public/Riding/RPGRidingCollection.h','Source/Project_RPG/Private/Riding/RPGRidingCollection.cpp','Source/Project_RPG/Public/Riding/RPGRidingOwnerTransport.h','Source/Project_RPG/Private/Riding/RPGRidingOwnerTransport.cpp','Source/Project_RPG/Public/Riding/RPGRidingCollectionReader.h','Source/Project_RPG/Private/Riding/RPGRidingCollectionReader.cpp','Source/Project_RPG/Private/Tests/RPGRidingCollectionTests.cpp','Source/Project_RPG/Public/Online/RPGBackendPlayerAuthState.h','Source/Project_RPG/Private/Online/RPGBackendPlayerAuthState.cpp')
    $files+=@('Source/Project_RPG/Public/Riding/RPGRidingCommand.h','Source/Project_RPG/Private/Riding/RPGRidingCommand.cpp','Source/Project_RPG/Private/Riding/RPGRidingIntentJson.h')
    if ($Screen) { $files+=@('Source/Project_RPG/Public/UI/Riding/RPGRidingCollectionWidget.h','Source/Project_RPG/Private/UI/Riding/RPGRidingCollectionWidget.cpp','Source/Project_RPG/Private/Tests/RPGRidingCollectionWidgetTests.cpp') }
    foreach ($relative in $files) {
        $source = Join-Path $workspace $relative
        $destination = Join-Path $project $relative
        $hash = (Get-FileHash -LiteralPath $source).Hash
        $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $destination).Hash -ne $hash -or (Get-FileHash -LiteralPath $source).Hash -ne $hash) { throw 'Source changed during capture' }
        $proof.Inputs += @{Path=$relative; SHA256=$hash}
    }
    $null = [IO.Directory]::CreateDirectory($proofRoot)
    $backend = Join-Path $temp 'BackendInputs'
    $fixtureDirectory = Join-Path $temp 'Fixtures'
    $null = [IO.Directory]::CreateDirectory($backend)
    & git -C $workspace archive --format=tar -o (Join-Path $temp 'Backend.tar') $proof.Head -- Backend
    if($LASTEXITCODE -ne 0){throw 'Backend archive failed'}
    & tar -xf (Join-Path $temp 'Backend.tar') -C $backend
    if($LASTEXITCODE -ne 0){throw 'Backend archive extraction failed'}
    foreach($relative in @('Backend/ProjectRpg.Backend.RidingHttpTests/OwnerReadHost.cs','Backend/ProjectRpg.Backend.RidingHttpTests/CommandHost.cs','Backend/ProjectRpg.Backend.RidingHttpTests/Program.cs')){
        $source=Join-Path $workspace $relative;$destination=Join-Path $backend $relative
        $hash=(Get-FileHash -LiteralPath $source).Hash
        Copy-Item -LiteralPath $source -Destination $destination
        if((Get-FileHash -LiteralPath $source).Hash -ne $hash -or (Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw 'Fixture source drift'}
        $proof.Inputs+=@{Path=$relative;SHA256=$hash}
    }
    $env:PROJECT_RPG_RIDING_OWNER_RUN=$id
    $env:PROJECT_RPG_RIDING_OWNER_FIXTURE=Join-Path $fixtureDirectory 'private.json'
    $backendProject=Join-Path $backend 'Backend/ProjectRpg.Backend.RidingHttpTests/ProjectRpg.Backend.RidingHttpTests.csproj'
    $env:DOTNET_CLI_HOME=Join-Path $temp 'Cli'
    $env:NUGET_PACKAGES=Join-Path $temp 'Nuget'
    $env:NUGET_HTTP_CACHE_PATH=Join-Path $temp 'NugetHttp'
    & dotnet build $backendProject -c Release --disable-build-servers -p:UseSharedCompilation=false *> (Join-Path $proofRoot 'BackendBuild.log')
    if($LASTEXITCODE -ne 0){Get-Content (Join-Path $proofRoot 'BackendBuild.log') -Tail 18;throw 'HTTP host build failed'}
    $backendModule=Join-Path $backend 'Backend/ProjectRpg.Backend.RidingHttpTests/bin/Release/net9.0/ProjectRpg.Backend.RidingHttpTests.dll'
    $proof.BackendProducerSHA256=(Get-FileHash -LiteralPath $backendModule).Hash
    $proof.BackendModuleSHA256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $backendModule) 'ProjectRpg.Backend.dll')).Hash
    $hostArgs=@(('"'+$backendModule+'"'),'--owner-read-host',('"'+$fixtureDirectory+'"'))
    $hostProcess=Start-Process (Get-Command dotnet).Source -ArgumentList $hostArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $temp 'Host.stdout') -RedirectStandardError (Join-Path $temp 'Host.stderr')
    $null=$hostProcess.Handle
    $readyDeadline=[DateTime]::UtcNow.AddSeconds(30)
    while(!(Test-Path -LiteralPath $env:PROJECT_RPG_RIDING_OWNER_FIXTURE)){
        if($hostProcess.HasExited){Get-Content (Join-Path $temp 'Host.stderr') -Tail 12;throw 'Owned host exited before ready'}
        if([DateTime]::UtcNow -ge $readyDeadline){throw 'Host readiness timed out'}
        Start-Sleep -Milliseconds 100
    }
    $proof.RunnerSHA256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash
    Write-Utf8 (Join-Path $project 'RidingOwnerRead.uproject') @'
{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}
'@
    Write-Utf8 (Join-Path $project 'Source/RidingOwnerReadEditor.Target.cs') @'
using UnrealBuildTool;
public class RidingOwnerReadEditorTarget : TargetRules {
    public RidingOwnerReadEditorTarget(TargetInfo Target) : base(Target) {
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
        PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "Json", "HTTP", "NetCore", "UMG", "Slate", "SlateCore", "InputCore"});
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Private/TradeClientModule.cpp') @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Project_RPG, "RidingOwnerRead");
'@
    $uproject = Join-Path $project 'RidingOwnerRead.uproject'
    if (!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt'))) { throw 'Installed engine required' }
    $build = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt = Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $buildArgs = @($ubt,'RidingOwnerReadEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),('-UBATraceOutputFile='+(Join-Path $temp 'Build.uba')),'-NoHotReloadFromIDE','-MaxParallelActions=2',('-log='+(Join-Path $proofRoot 'Build.log')))
    $proof.BuildCommand = @($build) + $buildArgs
    Save-Proof
    Write-Output ('Building isolated Riding owner reader; proof: '+$proofRoot)
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
    $editorArgs = @(('"'+$uproject+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG"','-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'))
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
    $expected = @('Wire','Malformed','ReaderLifetime','ReaderFailures','MemoryHttp') | ForEach-Object {'ProjectRPG.Riding.OwnerRead.'+$_}
    if ($Screen) { $expected += @('Browse','Filters','Context','Lifetime','Failures','MemoryHttp') | ForEach-Object {'ProjectRPG.Riding.CollectionScreen.'+$_} }
    if ((Compare-Object $expected @($proof.Tests.fullTestPath)) -or @($proof.Tests | Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0 -or $_.warnings -ne 0}).Count) { throw 'Missing or failing native cases' }
    foreach ($inputFile in $proof.Inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash -ne $inputFile.SHA256) { throw 'Original source changed during validation' }
    }
    if ((Get-FileHash -LiteralPath $PSCommandPath).Hash -ne $proof.RunnerSHA256) { throw 'Runner changed during validation' }
    $proof.Passed = $true
} catch { $proof.Failure = $_.Exception.Message }
finally {
    foreach($key in $savedSdkEnvironment.Keys){[Environment]::SetEnvironmentVariable($key,$savedSdkEnvironment[$key],'Process')}
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_RIDING_OWNER_RUN',$savedRun,'Process')
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_RIDING_OWNER_FIXTURE',$savedFixtures,'Process')
    $released = $true
    if ($process) {
        try {
            if (!$process.HasExited) { $process.Kill() }
            if (!$process.WaitForExit(10000)) { throw 'Owned test process still alive' }
        } catch { $released=$false; $proof.Failure=$_.Exception.Message }
    }
    if($hostProcess){
        try {
            if(!$hostProcess.HasExited){Write-Utf8 (Join-Path $fixtureDirectory 'stop') '';if(!$hostProcess.WaitForExit(10000)){$hostProcess.Kill()}}
            if(!$hostProcess.WaitForExit(10000)){throw 'Owned HTTP host still active'}
            if($hostProcess.ExitCode -ne 0){throw 'Owned HTTP host failed'}
            $statsFile=Join-Path $fixtureDirectory 'stats.json'
            if(Test-Path -LiteralPath $statsFile){
                $proof.Http=Get-Content -LiteralPath $statsFile -Raw -Encoding UTF8 | ConvertFrom-Json
                if($proof.Passed -and !$proof.Http.unchanged){throw 'Owner reads changed persistent state'}
            } elseif($proof.Passed){throw 'Missing HTTP outcome statistics'}
        } catch {$released=$released -and $hostProcess.HasExited;$proof.Failure=$_.Exception.Message}
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
