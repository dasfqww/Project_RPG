[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'));$id=[Guid]::NewGuid().ToString('N')
$root=Join-Path $workspace 'Saved/BuildChecks/TradingWindowLayout-20261114';$proof=Join-Path $root $id
$allowed=Join-Path $workspace 'Saved/TW';$temp=Join-Path $allowed $id.Substring(0,8);$project=Join-Path $temp 'Project'
$state=[ordered]@{Passed=$false;Run=$id;Head=$null;Inputs=@();BuildCommand=@();BuildExit=$null;ModuleSHA256=$null;Tests=@();NativeExit=$null;Captures=@();Cleanup=$null;CleanupComplete=$false;Failure=$null}
$process=$null;$ownsTemp=$false;$oldCapture=[Environment]::GetEnvironmentVariable('RPG_TRADING_CAPTURE_DIR','Process')
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Reserve {if(@(Get-Content (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object{$_ -match '^\|[^|]* / trading-window-layout-20261114 \|'}).Count -ne 1){throw 'Own reservation missing'}}
function Write-Utf8([string]$Path,[string]$Text){$null=[IO.Directory]::CreateDirectory((Split-Path $Path));[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Save {Write-Utf8 (Join-Path $proof 'Result.json') ($state|ConvertTo-Json -Depth 12)}
try{
 Reserve;$null=[IO.Directory]::CreateDirectory($allowed);$null=Assert-GuardianArtifactPath $temp $allowed
 if(Test-Path -LiteralPath $temp){throw 'Own temporary path exists'}
 $null=[IO.Directory]::CreateDirectory($project);$ownsTemp=$true;$null=[IO.Directory]::CreateDirectory($proof)
 $state.Head=(git -C $workspace rev-parse HEAD).Trim();if($LASTEXITCODE){throw 'Cannot resolve snapshot'}
 & git -C $workspace archive --format=tar -o (Join-Path $temp 'Plugin.tar') $state.Head -- Plugins/RPGInventory;if($LASTEXITCODE){throw 'Plugin archive failed'}
 & tar -xf (Join-Path $temp 'Plugin.tar') -C $project;if($LASTEXITCODE){throw 'Plugin extraction failed'}
 $state.PluginTree=(git -C $workspace rev-parse ($state.Head+':Plugins/RPGInventory')).Trim()
 $files=@('Source/Project_RPG/Private/Trading/RPGTradeJson.h','Source/Project_RPG/Public/Economy/RPGCurrencyTypes.h','Source/Project_RPG/Public/Economy/Backend/RPGEconomyBackendTypes.h',
  'Source/Project_RPG/Private/Auction/RPGAuctionCoordinator.View.cpp','Source/Project_RPG/Private/Auction/RPGAuctionCoordinator.Registration.cpp',
  'Source/Project_RPG/Private/Tests/RPGTradingWindowLayoutTests.cpp','Source/Project_RPG/Private/Tests/RPGTradeLobbyTests.cpp')
 foreach($stem in @('Trading/RPGTradeClient','Trading/RPGTradeTransport','Trading/RPGTradeCoordinator','Trading/RPGTradeLobbyOwner','Trading/RPGTradePreview','Trading/RPGTradeOfferDraft','Trading/RPGTradeTargets','Trading/RPGTradeAssets','Trading/RPGTradeIntentJournal',
  'Auction/RPGAuctionClient','Auction/RPGAuctionTransport','Auction/RPGAuctionIntentJournal','Auction/RPGAuctionCoordinator',
  'UI/Lobby/RPGTradeWidget','UI/Lobby/RPGAuctionWidget','UI/Lobby/RPGTradingWindowLayout','Online/RPGBackendPlayerAuthState','Economy/Backend/RPGEconomyBackendJsonCodec')){
  $files+=('Source/Project_RPG/Public/'+$stem+'.h');$files+=('Source/Project_RPG/Private/'+$stem+'.cpp')
 }
 foreach($relative in $files){$from=Join-Path $workspace $relative;$to=Join-Path $project $relative;$hash=(Get-FileHash -LiteralPath $from).Hash;$null=[IO.Directory]::CreateDirectory((Split-Path $to));Copy-Item -LiteralPath $from -Destination $to;if((Get-FileHash -LiteralPath $to).Hash -ne $hash){throw 'Input capture changed'};$state.Inputs+=@{Path=$relative;SHA256=$hash}}
 $state.RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash
 Write-Utf8 (Join-Path $project 'TradingWindow.uproject') '{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Plugins":[{"Name":"RPGInventory","Enabled":true}],"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}'
 Write-Utf8 (Join-Path $project 'Source/TradingWindowEditor.Target.cs') @'
using UnrealBuildTool;
public class TradingWindowEditorTarget:TargetRules {public TradingWindowEditorTarget(TargetInfo Target):base(Target){Type=TargetType.Editor;DefaultBuildSettings=BuildSettingsVersion.V7;IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8;ExtraModuleNames.Add("Project_RPG");}}
'@
 Write-Utf8 (Join-Path $project 'Source/Project_RPG/Project_RPG.Build.cs') @'
using UnrealBuildTool;
public class Project_RPG:ModuleRules {public Project_RPG(ReadOnlyTargetRules Target):base(Target){PCHUsage=PCHUsageMode.NoPCHs;bUseUnity=false;AddEngineThirdPartyPrivateStaticDependencies(Target,"OpenSSL");PublicDependencyModuleNames.AddRange(new string[]{"RPGInventoryRuntime","Core","CoreUObject","Engine","Json","HTTP","UMG","Slate","SlateCore","InputCore","RenderCore","RHI"});}}
'@
 Write-Utf8 (Join-Path $project 'Source/Project_RPG/Private/Module.cpp') ('#include "Modules/ModuleManager.h"'+"`n"+'IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl,Project_RPG,"TradingWindow");')
 $dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe';$ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll';$uproject=Join-Path $project 'TradingWindow.uproject'
 $arguments=@($ubt,'TradingWindowEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),('-UBATraceOutputFile='+(Join-Path $temp 'Build.uba')),'-NoHotReloadFromIDE','-MaxParallelActions=6',('-log='+(Join-Path $proof 'Build.log')))
 $state.BuildCommand=@($dotnet)+$arguments;Save;Write-Output ('BUILD trading window '+$proof)
 & $dotnet @arguments *> (Join-Path $proof 'BuildConsole.log');$state.BuildExit=$LASTEXITCODE;if($LASTEXITCODE){throw 'Native window target build failed'}
 $state.ModuleSHA256=(Get-FileHash -LiteralPath (Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll')).Hash
 & $dotnet $ubt '-Mode=QueryTargets' ('-Project='+$uproject) ('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')) '-IncludeAllTargets' '-DontIncludeParentAssembly' '-DontIncludeProgramTargets' '-NoMutex' '-SkipRulesCompile' '-NoLog' *> (Join-Path $proof 'Query.log');if($LASTEXITCODE){throw 'Target query failed'}
 $capture=Join-Path $proof 'Captures';$null=[IO.Directory]::CreateDirectory($capture);$env:RPG_TRADING_CAPTURE_DIR=$capture
 $report=Join-Path $temp 'Report';$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
 $arguments=@(('"'+$uproject+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG.Trading"','-TestExit="Automation Test Queue Empty"','-unattended','-d3d12','-RenderOffScreen','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proof 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'))
 $state.NativeCommand=@($editor)+$arguments;Save;Reserve
 $process=Start-Process $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru;$null=$process.Handle;$deadline=[DateTime]::UtcNow.AddSeconds(300)
 while(!$process.HasExited){if([DateTime]::UtcNow -gt $deadline){throw 'Own GPU automation deadline'};$null=$process.WaitForExit(200)};$process.WaitForExit();$state.NativeExit=$process.ExitCode
 $summary=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8|ConvertFrom-Json;$state.Tests=@($summary.tests|Select-Object fullTestPath,state,errors,warnings)
 $state.Captures=@(Get-ChildItem -LiteralPath $capture -Filter '*.png' -File|ForEach-Object{@{Path=$_.FullName;Bytes=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}});Save
 if($state.NativeExit -ne 0 -or $state.Tests.Count -ne 8 -or @($state.Tests|Where-Object{$_.state -ne 'Success' -or $_.errors -ne 0 -or $_.warnings -ne 0}).Count -or $state.Captures.Count -ne 6){throw 'GPU/native regression or capture set failed'}
 foreach($entry in $state.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash -ne $entry.SHA256){throw 'Input changed after verification'}}
 if((Get-FileHash -LiteralPath $PSCommandPath).Hash -ne $state.RunnerSHA256){throw 'Runner changed after verification'}
 $state.Passed=$true
}catch{$state.Failure=$_.Exception.Message}
finally{
 try{if($process -and !$process.HasExited){$process.Kill();if(!$process.WaitForExit(15000)){throw 'Own runtime did not exit'}};Reserve;$active=@(Get-CimInstance Win32_Process|Where-Object{$_.Name -match '^(dotnet|UnrealEditor.*|cl|link)\.exe$' -and $_.CommandLine -and $_.CommandLine.Replace('/','\').Contains($temp.Replace('/','\'))});if($active.Count){throw 'Own inputs still active'};if($ownsTemp -and (Test-Path -LiteralPath $temp)){$state.Cleanup=Remove-GuardianProbeArtifact $temp $allowed}}catch{$state.Failure=([string]$state.Failure+'; cleanup: '+$_.Exception.Message)}
 [Environment]::SetEnvironmentVariable('RPG_TRADING_CAPTURE_DIR',$oldCapture,'Process');$state.CleanupComplete=!(Test-Path -LiteralPath $temp);$state.HistoricalHashesOnly=$true;$state.Passed=$state.Passed -and !$state.Failure -and $state.CleanupComplete;Save;Write-Output ('Passed='+$state.Passed+'; '+$proof)
}
if(!$state.Passed){throw $state.Failure}
