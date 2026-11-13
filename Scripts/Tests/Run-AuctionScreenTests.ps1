[CmdletBinding()]
param([string]$EngineRoot = 'D:/UE_5.8', [switch]$Confirmation, [switch]$Proceeds)
$ErrorActionPreference = 'Stop'
if($Confirmation -and $Proceeds){throw 'Select one validation mode'}
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id = [Guid]::NewGuid().ToString('N')
$proofFolder=if($Proceeds){'AuctionProceeds-20261113'}elseif($Confirmation){'AuctionConfirm-20261113'}else{'AuctionScreen-20261113'}
$reservation=if($Proceeds){'auction-proceeds-20261113'}elseif($Confirmation){'auction-confirm-20261113'}else{'auction-screen-20261113'}
$proofRoot = Join-Path $workspace ('Saved/BuildChecks/'+$proofFolder+'/' + $id)
$tempRoot = Join-Path $workspace 'Saved/AS'
$temp = Join-Path $tempRoot $id.Substring(0,8)
$project = Join-Path $temp 'Project'
$proof = [ordered]@{Passed=$false; Scope='Actual native UMG proceeds query/claim including partial/zero results, purchase/cancel confirmation and read/recovery widget plus coordinator consent/pagination/lifetime/storage and original asset/client/recovery regressions; real mapper fixtures, fake HTTP, separate UE processes. No full lobby viewport, Steam, actual auction HTTP roundtrip or full game module link'; Inputs=@(); Tests=@(); Cleanup=$null; Failure=$null}
$process = $null
$ownsTemp = $false
$savedAssetsRun=[Environment]::GetEnvironmentVariable('PROJECT_RPG_AUCTION_ASSETS_RUN','Process')
$savedRun=[Environment]::GetEnvironmentVariable('PROJECT_RPG_AUCTION_CLIENT_RUN','Process')
$savedFixtures=[Environment]::GetEnvironmentVariable('PROJECT_RPG_AUCTION_WIRE_FIXTURE','Process')
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path, [string]$Text) {
    $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Save-Proof { Write-Utf8 (Join-Path $proofRoot 'Result.json') ($proof | ConvertTo-Json -Depth 10) }
function Assert-Reservation {
    $rows = @(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') | Where-Object {$_ -match ('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')})
    if ($rows.Count -ne 1) { throw 'Auction screen validation reservation missing' }
}
try {
    Assert-Reservation
    $ancestor=$temp
    while($ancestor){if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor rejected'};$ancestor=[IO.Path]::GetDirectoryName($ancestor)}
    if (Test-Path -LiteralPath $temp) { throw 'Owned temporary path already exists' }
    $null = [IO.Directory]::CreateDirectory((Join-Path $project 'Content'))
    $ownsTemp = $true
    $proof.Head = (git -C $workspace rev-parse HEAD).Trim()
    $files = @('Source/Project_RPG/Public/Trading/RPGTradeClient.h','Source/Project_RPG/Private/Trading/RPGTradeClient.cpp','Source/Project_RPG/Private/Trading/RPGTradeJson.h')
    $files += @('Source/Project_RPG/Public/Trading/RPGTradePreview.h','Source/Project_RPG/Private/Trading/RPGTradePreview.cpp','Source/Project_RPG/Public/Auction/RPGAuctionClient.h','Source/Project_RPG/Private/Auction/RPGAuctionClient.cpp','Source/Project_RPG/Private/Tests/RPGAuctionClientTests.cpp')
    $files += @('Source/Project_RPG/Public/Auction/RPGAuctionIntentJournal.h','Source/Project_RPG/Private/Auction/RPGAuctionIntentJournal.cpp','Source/Project_RPG/Public/Auction/RPGAuctionCoordinator.h','Source/Project_RPG/Private/Auction/RPGAuctionCoordinator.cpp','Source/Project_RPG/Private/Auction/RPGAuctionCoordinator.View.cpp','Source/Project_RPG/Public/Auction/RPGAuctionTransport.h','Source/Project_RPG/Private/Auction/RPGAuctionTransport.cpp','Source/Project_RPG/Private/Tests/RPGAuctionRecoveryTests.cpp')
    $files+=@('Source/Project_RPG/Public/Trading/RPGTradeAssets.h','Source/Project_RPG/Private/Trading/RPGTradeAssets.cpp','Source/Project_RPG/Public/Economy/RPGCurrencyTypes.h','Source/Project_RPG/Public/Economy/Backend/RPGEconomyBackendTypes.h','Source/Project_RPG/Public/Economy/Backend/RPGEconomyBackendJsonCodec.h','Source/Project_RPG/Private/Economy/Backend/RPGEconomyBackendJsonCodec.cpp','Source/Project_RPG/Private/Tests/RPGAuctionAssetsTests.cpp','Source/Project_RPG/Public/UI/Lobby/RPGAuctionWidget.h','Source/Project_RPG/Private/UI/Lobby/RPGAuctionWidget.cpp','Source/Project_RPG/Private/Tests/RPGAuctionScreenTests.cpp')
    $archive=Join-Path $temp 'Inventory.tar'
    & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- Plugins/RPGInventory
    if($LASTEXITCODE -ne 0){throw 'Plugin snapshot failed'}
    & tar -xf $archive -C $project
    if($LASTEXITCODE -ne 0){throw 'Plugin extraction failed'}
    $proof.PluginTree=(git -C $workspace rev-parse ($proof.Head+':Plugins/RPGInventory')).Trim()
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
    foreach($relative in @('Backend/ProjectRpg.Backend/Domain/Trading/AuctionAssets.cs','Backend/ProjectRpg.Backend/Data/IAuctionQueryRepository.cs','Backend/ProjectRpg.Backend/Data/InMemoryAuctionRepository.Queries.cs','Backend/ProjectRpg.Backend/Data/PostgresAuctionQueryRepository.cs','Backend/ProjectRpg.Backend/Data/PostgresAuctionQueryRepository.Assets.cs','Backend/ProjectRpg.Backend/Application/AuctionApplication.cs','Backend/ProjectRpg.Backend/Api/AuctionApiEndpoints.cs','Backend/ProjectRpg.Backend/Api/AuctionApiContracts.cs','Backend/ProjectRpg.Backend.TradingTests/AuctionWireFixtures.cs','Backend/ProjectRpg.Backend.TradingTests/Program.cs')){
        $source=Join-Path $workspace $relative;$destination=Join-Path $backend $relative
        $hash=(Get-FileHash -LiteralPath $source).Hash
        Copy-Item -LiteralPath $source -Destination $destination
        if((Get-FileHash -LiteralPath $source).Hash -ne $hash -or (Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw 'Fixture source drift'}
        $proof.Inputs+=@{Path=$relative;SHA256=$hash}
    }
    $env:PROJECT_RPG_AUCTION_ASSETS_RUN=$id
    $env:PROJECT_RPG_AUCTION_CLIENT_RUN=$id
    $env:PROJECT_RPG_AUCTION_WIRE_FIXTURE=$fixtureDirectory
    $backendProject=Join-Path $backend 'Backend/ProjectRpg.Backend.TradingTests/ProjectRpg.Backend.TradingTests.csproj'
    & dotnet build $backendProject -c Release --disable-build-servers -p:UseSharedCompilation=false *> (Join-Path $proofRoot 'BackendBuild.log')
    if($LASTEXITCODE -ne 0){Get-Content (Join-Path $proofRoot 'BackendBuild.log') -Tail 18;throw 'Wire producer build failed'}
    $backendModule=Join-Path $backend 'Backend/ProjectRpg.Backend.TradingTests/bin/Release/net9.0/ProjectRpg.Backend.TradingTests.dll'
    $proof.BackendProducerSHA256=(Get-FileHash -LiteralPath $backendModule).Hash
    $proof.BackendModuleSHA256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $backendModule) 'ProjectRpg.Backend.dll')).Hash
    $proof.BackendCommands=@(@('dotnet','build',$backendProject,'-c','Release','--disable-build-servers','-p:UseSharedCompilation=false'),@('dotnet',$backendModule,'--auction-wire-fixtures',$fixtureDirectory))
    & dotnet $backendModule --auction-wire-fixtures $fixtureDirectory *> (Join-Path $proofRoot 'BackendFixtures.log')
    if($LASTEXITCODE -ne 0){Get-Content (Join-Path $proofRoot 'BackendFixtures.log') -Tail 18;throw 'Wire producer failed'}
    $proof.Fixtures=@(Get-ChildItem -LiteralPath $fixtureDirectory -Filter '*.json' -File | ForEach-Object {@{Name=$_.Name;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash;Bytes=$_.Length}})
    if($proof.Fixtures.Count -ne 14){throw 'Missing contract fixture documents'}
    $proof.RunnerSHA256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash
    Write-Utf8 (Join-Path $project 'AuctionRecovery.uproject') @'
{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Plugins":[{"Name":"RPGInventory","Enabled":true}],"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}
'@
    Write-Utf8 (Join-Path $project 'Source/AuctionRecoveryEditor.Target.cs') @'
using UnrealBuildTool;
public class AuctionRecoveryEditorTarget : TargetRules {
    public AuctionRecoveryEditorTarget(TargetInfo Target) : base(Target) {
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
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        PublicDependencyModuleNames.AddRange(new string[] {"RPGInventoryRuntime", "Core", "CoreUObject", "Engine", "Json", "HTTP", "UMG", "Slate", "SlateCore", "InputCore"});
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Private/TradeClientModule.cpp') @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Project_RPG, "AuctionRecovery");
'@
    # Synthetic fixture tags belong only to this isolated project, never the production tag dictionary.
    Write-Utf8 (Join-Path $project 'Config/DefaultGameplayTags.ini') @'
[/Script/GameplayTags.GameplayTagsSettings]
ImportTagsFromConfig=True
+GameplayTagList=(Tag="Item.Ore",DevComment="Owned backend test fixture")
+GameplayTagList=(Tag="Stat.Value",DevComment="Owned backend test fixture")
'@
    $uproject = Join-Path $project 'AuctionRecovery.uproject'
    if (!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt'))) { throw 'Installed engine required' }
    $build = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt = Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $buildArgs = @($ubt,'AuctionRecoveryEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),('-UBATraceOutputFile='+(Join-Path $temp 'Build.uba')),'-NoHotReloadFromIDE','-MaxParallelActions=1',('-log='+(Join-Path $proofRoot 'Build.log')))
    $proof.BuildCommand = @($build) + $buildArgs
    Save-Proof
    Write-Output ('Building isolated auction screen; proof: '+$proofRoot)
    & $build @buildArgs *> (Join-Path $proofRoot 'BuildConsole.log')
    $proof.BuildExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Isolated C++ build failed' }
    $proof.ModuleSHA256 = (Get-FileHash -LiteralPath (Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll')).Hash
    $queryArgs = @($ubt,'-Mode=QueryTargets',('-Project='+$uproject),('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')),'-IncludeAllTargets','-DontIncludeParentAssembly','-DontIncludeProgramTargets','-NoMutex','-SkipRulesCompile','-NoLog')
    & $build @queryArgs *> (Join-Path $proofRoot 'Query.log')
    $proof.QueryExit = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'Isolated target query failed' }
    $fixtureRoot = Join-Path $temp 'JournalFixture'
    $null=[IO.Directory]::CreateDirectory($fixtureRoot)
    $target=Join-Path $fixtureRoot 'LinkTarget'
    $null=[IO.Directory]::CreateDirectory($target)
    $null=New-Item -ItemType Junction -Path (Join-Path $fixtureRoot 'Linked') -Target $target
    $proof.Phases=@()
    foreach($phase in @('write','read')) {
        Assert-Reservation
        $report=Join-Path $temp ('Report-'+$phase)
        $editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
        $filter=if($phase -eq 'write'){'ProjectRPG.Auction'}else{'ProjectRPG.Auction.Recovery.Restart'}
        $editorArgs=@(('"'+$uproject+'"'),'/Engine/Maps/Entry',('-ExecCmds="Automation RunTests '+$filter+'"'),'-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp ('User-'+$phase))+'"'),('-abslog="'+(Join-Path $proofRoot ($phase+'.log'))+'"'),('-ReportExportPath="'+$report+'"'),('-AuctionJournalTestRoot="'+$fixtureRoot+'"'),('-AuctionJournalPhase='+$phase))
        $process=Start-Process $editor -ArgumentList $editorArgs -WindowStyle Hidden -PassThru
        $null=$process.Handle
        $deadline=[DateTime]::UtcNow.AddSeconds(300)
        while(!$process.HasExited){if([DateTime]::UtcNow -ge $deadline){throw 'Owned automation timed out'};$null=$process.WaitForExit(200)}
        $process.WaitForExit()
        $result=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8|ConvertFrom-Json
        $tests=@($result.tests|Select-Object fullTestPath,state,errors,warnings)
        $proof.Phases+=@{Phase=$phase;ProcessId=$process.Id;Exit=$process.ExitCode;Command=@($editor)+$editorArgs;Tests=$tests}
        $proof.Tests+=$tests
        Save-Proof
        if($process.ExitCode -ne 0){throw 'Native process failed'}
        $expected=@('ProjectRPG.Auction.Recovery.Restart')
        if($phase -eq 'write'){
            $expected=@('Wire','Commands','Receipts','UncertainRecovery','Lifetime','Intent')|ForEach-Object {'ProjectRPG.Auction.Client.'+$_}
            $expected+=@('Storage','Locking','Coordinator','Context','StorageFailure','Restart')|ForEach-Object {'ProjectRPG.Auction.Recovery.'+$_}
            $expected+=@('Codec','Projection','Lifetime')|ForEach-Object {'ProjectRPG.Auction.Assets.'+$_}
            $expected+=@('Browse','ContextAndThrottle','WidgetAndRecovery','StorageRecheck','PurchaseConfirmation','CancelConfirmation','ConfirmationLifetime','ProceedsConfirmation','ProceedsReadSafety','ProceedsZeroRecovery')|ForEach-Object {'ProjectRPG.Auction.Screen.'+$_}
        }
        if((Compare-Object $expected @($tests.fullTestPath)) -or @($tests|Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0 -or $_.warnings -ne 0}).Count){throw ('Missing or failing native cases: '+$phase)}
    }
    foreach ($inputFile in $proof.Inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash -ne $inputFile.SHA256) { throw 'Original source changed during validation' }
    }
    if ((Get-FileHash -LiteralPath $PSCommandPath).Hash -ne $proof.RunnerSHA256) { throw 'Runner changed during validation' }
    $proof.Passed = $true
} catch { $proof.Failure = $_.Exception.Message }
finally {
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_AUCTION_CLIENT_RUN',$savedRun,'Process')
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_AUCTION_WIRE_FIXTURE',$savedFixtures,'Process')
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_AUCTION_ASSETS_RUN',$savedAssetsRun,'Process')
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
