[CmdletBinding()]
param([string]$EngineRoot = 'D:/UE_5.8',
    [ValidateSet('trading-assets-ui-20261109','trading-assets-session-scope-20261110')]
    [string]$ReservationId = 'trading-assets-ui-20261109',
    [ValidateSet('ProjectRPG.Trading','ProjectRPG.Trading.Assets')]
    [string]$TestFilter = 'ProjectRPG.Trading')
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id = [Guid]::NewGuid().ToString('N')
$proofRoot = Join-Path $workspace ('Saved/BuildChecks/TradeAssetsUi-20261109/' + $id)
$tempRoot = Join-Path $workspace 'Saved/AU'
$temp = Join-Path $tempRoot $id.Substring(0,8)
$project = Join-Path $temp 'Project'
$proof = [ordered]@{Passed=$false; Scope='Actual target discovery/explicit UMG selection/local owner and immutable invite HTTP replay plus trading regression; no full-game viewport/Steam/backend integration'; Inputs=@(); Tests=@(); Cleanup=$null; Failure=$null}
$process = $null
$fixture = $null
$ownsTemp = $false
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path, [string]$Text) {
    $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Save-Proof { Write-Utf8 (Join-Path $proofRoot 'Result.json') ($proof | ConvertTo-Json -Depth 10) }
function Assert-Reservation {
    $rows = @(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') | Where-Object {$_ -match ('^\|[^|]* / '+[regex]::Escape($ReservationId)+' \|')})
    if ($rows.Count -ne 1) { throw 'Trading client validation reservation missing' }
}
try {
    Assert-Reservation
    if (Test-Path -LiteralPath $temp) { throw 'Owned temporary path already exists' }
    $null = [IO.Directory]::CreateDirectory((Join-Path $project 'Content'))
    $ownsTemp = $true
    $proof.Head = (git -C $workspace rev-parse HEAD).Trim()
    $files = @('Source/Project_RPG/Public/Trading/RPGTradeClient.h','Source/Project_RPG/Private/Trading/RPGTradeClient.cpp','Source/Project_RPG/Private/Tests/RPGTradeClientTests.cpp','Source/Project_RPG/Public/Trading/RPGTradeTransport.h','Source/Project_RPG/Private/Trading/RPGTradeTransport.cpp','Source/Project_RPG/Public/Trading/RPGTradeCoordinator.h','Source/Project_RPG/Private/Trading/RPGTradeCoordinator.cpp','Source/Project_RPG/Private/Tests/RPGTradeTransportTests.cpp','Scripts/Tests/trade_assets_ui_fixture.py','Source/Project_RPG/Public/Trading/RPGTradeLobbyOwner.h','Source/Project_RPG/Private/Trading/RPGTradeLobbyOwner.cpp','Source/Project_RPG/Public/UI/Lobby/RPGTradeWidget.h','Source/Project_RPG/Private/UI/Lobby/RPGTradeWidget.cpp','Source/Project_RPG/Private/Tests/RPGTradeLobbyTests.cpp','Source/Project_RPG/Private/Trading/RPGTradeJson.h','Source/Project_RPG/Public/Trading/RPGTradePreview.h','Source/Project_RPG/Private/Trading/RPGTradePreview.cpp','Source/Project_RPG/Private/Tests/RPGTradePreviewTests.cpp','Source/Project_RPG/Public/Trading/RPGTradeOfferDraft.h','Source/Project_RPG/Private/Trading/RPGTradeOfferDraft.cpp','Source/Project_RPG/Private/Tests/RPGTradeOfferTests.cpp')
    $files += @('Source/Project_RPG/Public/Trading/RPGTradeTargets.h','Source/Project_RPG/Private/Trading/RPGTradeTargets.cpp')
    $files += @('Source/Project_RPG/Private/Tests/RPGTradeTargetUiTests.cpp')
    $files += @('Source/Project_RPG/Public/Trading/RPGTradeAssets.h','Source/Project_RPG/Private/Trading/RPGTradeAssets.cpp','Source/Project_RPG/Public/Economy/RPGCurrencyTypes.h','Source/Project_RPG/Public/Economy/Backend/RPGEconomyBackendTypes.h','Source/Project_RPG/Public/Economy/Backend/RPGEconomyBackendJsonCodec.h','Source/Project_RPG/Private/Economy/Backend/RPGEconomyBackendJsonCodec.cpp')
    $files += @('Source/Project_RPG/Private/Tests/RPGTradeAssetsUiTests.cpp','Source/Project_RPG/Public/Progression/RPGEquipmentReadGuard.h')
    $files += @('Source/Project_RPG/Public/Trading/RPGTradeIntentJournal.h','Source/Project_RPG/Private/Trading/RPGTradeIntentJournal.cpp')
    foreach ($relative in $files) {
        $source = Join-Path $workspace $relative
        $destination = Join-Path $project $relative
        $hash = (Get-FileHash -LiteralPath $source).Hash
        $null = [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        if ((Get-FileHash -LiteralPath $destination).Hash -ne $hash -or (Get-FileHash -LiteralPath $source).Hash -ne $hash) { throw 'Source changed during capture' }
        $proof.Inputs += @{Path=$relative; SHA256=$hash}
    }
    # Published plugin only; never copy another agent's working plugin edits.
    $proof.PluginTree=(git -C $workspace rev-parse ($proof.Head+':Plugins/RPGInventory')).Trim()
    $pluginArchive=Join-Path $temp 'InventoryPlugin.tar'
    & git -C $workspace archive --format=tar ('-o'+$pluginArchive) $proof.Head -- Plugins/RPGInventory
    if($LASTEXITCODE -ne 0){throw 'Published inventory plugin archive failed'}
    & tar -xf $pluginArchive -C $project
    if($LASTEXITCODE -ne 0){throw 'Published inventory plugin extraction failed'}
    $proof.RunnerSHA256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash
    Write-Utf8 (Join-Path $project 'TradeClient.uproject') @'
{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Plugins":[{"Name":"RPGInventory","Enabled":true}],"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}
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
        PublicDependencyModuleNames.AddRange(new string[] {"RPGInventoryRuntime", "Core", "CoreUObject", "Engine", "Json", "HTTP", "UMG", "Slate", "SlateCore", "InputCore"});
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
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
    $editorArgs = @(('"'+$uproject+'"'),'/Engine/Maps/Entry',('-ExecCmds="Automation RunTests '+$TestFilter+'"'),'-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'))
    $portFile = Join-Path $temp 'Port.txt'
    $statsFile = Join-Path $temp 'HttpStats.json'
    $python = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
    if (!(Test-Path -LiteralPath $python)) { throw 'Engine Python required for the owned loopback fixture' }
    $fixtureArgs = @(('"'+(Join-Path $project 'Scripts/Tests/trade_assets_ui_fixture.py')+'"'),'--port-file',('"'+$portFile+'"'),'--stats-file',('"'+$statsFile+'"'))
    $fixture = Start-Process $python -ArgumentList $fixtureArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $proofRoot 'Fixture.log') -RedirectStandardError (Join-Path $proofRoot 'FixtureError.log')
    $null = $fixture.Handle
    $fixtureDeadline = [DateTime]::UtcNow.AddSeconds(60)
    $port = 0
    while ($port -lt 1 -or $port -gt 65535) {
        if ($fixture.HasExited -or [DateTime]::UtcNow -ge $fixtureDeadline) {
            $proof.FixtureExited = $fixture.HasExited
            if ($fixture.HasExited) { $proof.FixtureExit = $fixture.ExitCode }
            throw 'Owned HTTP fixture startup failed'
        }
        if (Test-Path -LiteralPath $portFile) {
            try {
                $portText = [IO.File]::ReadAllText($portFile).Trim()
                $parsedPort = 0
                if ([int]::TryParse($portText, [ref]$parsedPort) -and $parsedPort -ge 1 -and $parsedPort -le 65535) { $port = $parsedPort }
            } catch [IO.IOException] { }
        }
        if ($port -ge 1 -and $port -le 65535) { break }
        [Threading.Thread]::Sleep(100)
    }
    $proof.FixturePort = $port
    $editorArgs += ('-TradeTestEndpoint=http://127.0.0.1:'+$port+'/api')
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
    $expected += @('AuthRetry','RetryBounds','ContextLifetime','ReadAndFailure','RealHttp') | ForEach-Object {'ProjectRPG.Trading.Transport.'+$_}
    $expected += @('ScreenLifetime','PauseAndRecovery','Identity','PollingAndControls','WidgetSafety') | ForEach-Object {'ProjectRPG.Trading.Lobby.'+$_}
    $expected += @('Codec','Versioning','Consent','FreshnessAndDefinitions','WidgetReview','RealHttp') | ForEach-Object {'ProjectRPG.Trading.Preview.'+$_}
    $expected += @('ValuesAndBounds','ScopeAndStaleness','ImmutableReplay','WidgetEditing','RealHttp') | ForEach-Object {'ProjectRPG.Trading.Offer.'+$_}
    $expected += @('Codec','SelectionAndReplay','ContextAndThrottle','WidgetSelection','RealHttp') | ForEach-Object {'ProjectRPG.Trading.Targets.'+$_}
    $expected += @('Codec','OwnerAndGeneration','NewTradeScope','ScopeAndPending','WidgetAndClose','RealHttp') | ForEach-Object {'ProjectRPG.Trading.Assets.'+$_}
    $expected = @($expected | Where-Object {$_.StartsWith($TestFilter+'.',[StringComparison]::Ordinal)})
    if ((Compare-Object $expected @($proof.Tests.fullTestPath)) -or @($proof.Tests | Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0 -or $_.warnings -ne 0}).Count) { throw 'Missing or failing native cases' }
    foreach ($inputFile in $proof.Inputs) {
        if ((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash -ne $inputFile.SHA256) { throw 'Original source changed during validation' }
    }
    if ((Get-FileHash -LiteralPath $PSCommandPath).Hash -ne $proof.RunnerSHA256) { throw 'Runner changed during validation' }
    $proof.HttpStats = Get-Content -LiteralPath $statsFile -Raw -Encoding UTF8 | ConvertFrom-Json
    $expectedWire = @{commits=2;posts=4;replays=2;gets=3;unauthorized=1;previews=1;offerPosts=2;offerCommits=1;offerReplays=1;targetGets=1;assetGets=1;assetActiveGets=1}
    if ($TestFilter -eq 'ProjectRPG.Trading.Assets') {foreach($key in @($expectedWire.Keys)){if($key -notin @('assetGets','assetActiveGets')){$expectedWire[$key]=0}}}
    foreach($key in $expectedWire.Keys){if($proof.HttpStats.$key -ne $expectedWire[$key]){throw ('HTTP fixture count failed: '+$key)}}
    if ($proof.HttpStats.errors.Count) { throw 'HTTP fixture contract failed' }
    $proof.Passed = $true
} catch { $proof.Failure = $_.Exception.Message }
finally {
    if ($statsFile -and (Test-Path -LiteralPath $statsFile)) { $proof.HttpStats=Get-Content -LiteralPath $statsFile -Raw -Encoding UTF8 | ConvertFrom-Json }
    $released = $true
    if ($process) {
        try {
            if (!$process.HasExited) { $process.Kill() }
            if (!$process.WaitForExit(10000)) { throw 'Owned test process still alive' }
        } catch { $released=$false; $proof.Failure=$_.Exception.Message }
    }
    if ($fixture) {
        try { if (!$fixture.HasExited) { $fixture.Kill() }; if (!$fixture.WaitForExit(10000)) { throw 'Owned fixture still alive' } }
        catch { $released=$false; $proof.Failure=$_.Exception.Message }
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
