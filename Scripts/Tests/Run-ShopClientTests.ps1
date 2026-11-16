[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run=[Guid]::NewGuid().ToString('N')
$tempRoot=Join-Path $workspace 'Saved/SH'
$temp=Join-Path $tempRoot $run.Substring(0,8)
$project=Join-Path $temp 'Project'
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/Shop-20261117/Native-'+$run)
$proof=[ordered]@{Passed=$false;Scope='Isolated native shop codec, consent, durable recovery, exact receipt and current asset import using fake HTTP. No live shop HTTP, full lobby, GPU, Steam or Shipping.';Head='';Inputs=@();Tests=@();Cleanup=$null;Failure=$null}
$process=$null
$ownsTemp=$false
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path,[string]$Value){$null=[IO.Directory]::CreateDirectory((Split-Path -Parent $Path));[IO.File]::WriteAllText($Path,$Value,[Text.UTF8Encoding]::new($false))}
function Save-Proof {Write-Utf8 (Join-Path $proofRoot 'Result.json') ($proof|ConvertTo-Json -Depth 10)}
function Assert-Reservation {
    $rows=@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object {$_ -match '^\|[^|]* / shop-mvp-20261117 \|'})
    if($rows.Count -ne 1){throw 'Shop reservation missing'}
}
try {
    Assert-Reservation
    $ancestor=$temp
    while($ancestor){if((Test-Path -LiteralPath $ancestor)-and((Get-Item -LiteralPath $ancestor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor'};$ancestor=[IO.Path]::GetDirectoryName($ancestor)}
    if(Test-Path -LiteralPath $temp){throw 'Owned temp exists'}
    $null=[IO.Directory]::CreateDirectory((Join-Path $project 'Content'));$ownsTemp=$true
    $null=[IO.Directory]::CreateDirectory($proofRoot)
    $proof.Head=(& git -C $workspace rev-parse HEAD).Trim()
    $baseline=@('Plugins/RPGInventory','Source/Project_RPG/Public/Trading/RPGTradeClient.h','Source/Project_RPG/Private/Trading/RPGTradeClient.cpp','Source/Project_RPG/Private/Trading/RPGTradeJson.h',
        'Source/Project_RPG/Public/Trading/RPGTradeAssets.h','Source/Project_RPG/Private/Trading/RPGTradeAssets.cpp',
        'Source/Project_RPG/Public/Economy/RPGCurrencyTypes.h','Source/Project_RPG/Public/Economy/Backend/RPGEconomyBackendTypes.h',
        'Source/Project_RPG/Public/Economy/Backend/RPGEconomyBackendJsonCodec.h','Source/Project_RPG/Private/Economy/Backend/RPGEconomyBackendJsonCodec.cpp')
    $archive=Join-Path $temp 'Inputs.tar'
    & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $baseline
    if($LASTEXITCODE -ne 0){throw 'Published native baseline archive failed'}
    & tar -xf $archive -C $project
    if($LASTEXITCODE -ne 0){throw 'Native baseline extraction failed'}
    $proof.PluginTree=(& git -C $workspace rev-parse ($proof.Head+':Plugins/RPGInventory')).Trim()
    $files=@('Source/Project_RPG/Public/Shop/RPGShopTypes.h','Source/Project_RPG/Private/Shop/RPGShopTypes.cpp',
        'Source/Project_RPG/Public/Shop/RPGShopTransport.h','Source/Project_RPG/Private/Shop/RPGShopTransport.cpp',
        'Source/Project_RPG/Public/Shop/RPGShopCoordinator.h','Source/Project_RPG/Private/Shop/RPGShopCoordinator.cpp',
        'Source/Project_RPG/Private/Shop/RPGShopJournal.h','Source/Project_RPG/Private/Shop/RPGShopJournal.cpp',
        'Source/Project_RPG/Private/Tests/RPGShopTests.cpp')
    foreach($relative in $files){
        $source=Join-Path $workspace $relative;$destination=Join-Path $project $relative;$hash=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination));Copy-Item -LiteralPath $source -Destination $destination -Force
        if((Get-FileHash -LiteralPath $destination).Hash -ne $hash-or(Get-FileHash -LiteralPath $source).Hash -ne $hash){throw 'Input changed during copy'}
        $proof.Inputs+=@{Path=$relative;SHA256=$hash}
    }
    $proof.RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash
    Write-Utf8 (Join-Path $project 'ShopNative.uproject') @'
{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Plugins":[{"Name":"RPGInventory","Enabled":true}],"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}
'@
    Write-Utf8 (Join-Path $project 'Source/ShopNativeEditor.Target.cs') @'
using UnrealBuildTool;
public class ShopNativeEditorTarget : TargetRules {
    public ShopNativeEditorTarget(TargetInfo Target) : base(Target) {
        Type=TargetType.Editor;DefaultBuildSettings=BuildSettingsVersion.V7;
        IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8;ExtraModuleNames.Add("Project_RPG");
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Project_RPG.Build.cs') @'
using UnrealBuildTool;
public class Project_RPG : ModuleRules {
    public Project_RPG(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage=PCHUsageMode.NoPCHs;bUseUnity=false;
        AddEngineThirdPartyPrivateStaticDependencies(Target,"OpenSSL");
        PublicDependencyModuleNames.AddRange(new string[]{"RPGInventoryRuntime","Core","CoreUObject","Engine","Json","HTTP"});
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Private/ShopNativeModule.cpp') @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl,Project_RPG,"ShopNative");
'@
    $uproject=Join-Path $project 'ShopNative.uproject'
    if(!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt'))){throw 'Installed engine required'}
    $dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $buildArgs=@($ubt,'ShopNativeEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),'-NoHotReloadFromIDE','-MaxParallelActions=2',('-log='+(Join-Path $proofRoot 'Build.log')))
    $proof.BuildCommand=@($dotnet)+$buildArgs;Save-Proof
    & $dotnet @buildArgs *> (Join-Path $proofRoot 'BuildConsole.log')
    $proof.BuildExit=$LASTEXITCODE
    if($LASTEXITCODE -ne 0){throw 'Shop native compile/link failed'}
    $proof.ModuleSHA256=(Get-FileHash -LiteralPath (Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll')).Hash
    $queryArgs=@($ubt,'-Mode=QueryTargets',('-Project='+$uproject),('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')),'-IncludeAllTargets','-DontIncludeParentAssembly','-DontIncludeProgramTargets','-NoMutex','-SkipRulesCompile','-NoLog')
    & $dotnet @queryArgs *> (Join-Path $proofRoot 'Query.log')
    if($LASTEXITCODE -ne 0){throw 'Native target query failed'}
    $report=Join-Path $temp 'Report';$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $editorArgs=@(('"'+$uproject+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG.Shop"','-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'))
    Assert-Reservation;$proof.NativeCommand=@($editor)+$editorArgs
    $process=Start-Process $editor -ArgumentList $editorArgs -WindowStyle Hidden -PassThru;$null=$process.Handle
    $deadline=[DateTime]::UtcNow.AddSeconds(240)
    while(!$process.HasExited){if([DateTime]::UtcNow-ge$deadline){throw 'Owned shop automation timeout'};$null=$process.WaitForExit(200)}
    $process.WaitForExit();$proof.NativeExit=$process.ExitCode
    $result=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $proof.Tests=@($result.tests|Select-Object fullTestPath,state,errors,warnings)
    $expected=@('ProjectRPG.Shop.Codec','ProjectRPG.Shop.Consent','ProjectRPG.Shop.Recovery','ProjectRPG.Shop.ScopeAndReceipt','ProjectRPG.Shop.RejectedRecovery')
    if($process.ExitCode-ne 0-or(Compare-Object $expected @($proof.Tests.fullTestPath))-or@($proof.Tests|Where-Object {$_.state-ne'Success'-or$_.errors-ne 0-or$_.warnings-ne 0}).Count){throw 'Native shop tests failed or missing'}
    foreach($inputFile in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash-ne$inputFile.SHA256){throw 'Verified shop source changed'}}
    if((Get-FileHash -LiteralPath $PSCommandPath).Hash-ne$proof.RunnerSHA256){throw 'Runner changed'}
    $proof.Passed=$true
}catch{$proof.Failure=$_.Exception.Message}
finally {
    $released=$true
    if($process){try{if(!$process.HasExited){$process.Kill()};if(!$process.WaitForExit(10000)){throw 'Owned native process still running'}}catch{$released=$false;$proof.Failure=$_.Exception.Message}}
    try{Assert-Reservation;if(!$released){throw 'Cannot remove active native files'};if($ownsTemp-and(Test-Path -LiteralPath $temp)){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}}catch{$proof.Failure=$_.Exception.Message}
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp);$proof.HistoricalHashesOnly=$true;$proof.Passed=$proof.Passed-and!$proof.Failure-and$proof.CleanupComplete
    Save-Proof;Write-Output ('Passed='+$proof.Passed+'; proof='+$proofRoot)
}
if(!$proof.Passed){throw $proof.Failure}
