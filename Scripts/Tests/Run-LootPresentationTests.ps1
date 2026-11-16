param([switch]$IntegrationOnly, [string]$EngineRoot='D:/UE_5.8')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/LootPresentation/'+[Guid]::NewGuid().ToString('N'))
$tempRoot=Join-Path $workspace 'Saved/LP'
$temp=Join-Path $tempRoot ([Guid]::NewGuid().ToString('N').Substring(0,8))
$engine=$EngineRoot
$dotnet=Join-Path $engine 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
$ubt=Join-Path $engine 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
$editor=Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
$proof=[ordered]@{Passed=$false;Head=(git -C $workspace rev-parse HEAD).Trim();Inputs=@();Temp=$temp;Failure=$null}
$process=$null
function Put([string]$Path,[string]$Text){[IO.Directory]::CreateDirectory((Split-Path -Parent $Path))|Out-Null;[IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))}
function Save {Put (Join-Path $proofRoot 'Checks.json') ($proof|ConvertTo-Json -Depth 9)}
function Build([string]$Target,[string]$Project,[string]$Label,[string[]]$Extra=@()) {
 $arguments=@($ubt,$Target,'Win64','Development',$Project,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote','-NoHotReloadFromIDE','-MaxParallelActions=2',('-log='+(Join-Path $proofRoot ($Label+'.log'))))+$Extra
 $proof[$Label+'Command']=@($dotnet)+$arguments;Save
 & $dotnet @arguments *> (Join-Path $proofRoot ($Label+'Console.log'))
 $proof[$Label+'Exit']=$LASTEXITCODE;Save
 if($LASTEXITCODE -ne 0){throw ($Label+' failed')}
}
try {
 if(!(Select-String -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -SimpleMatch '/ loot-presentation-20261115 |' -Quiet -Encoding UTF8)){throw 'Reservation missing'}
 [IO.Directory]::CreateDirectory($temp)|Out-Null
 $files=@('Source/Project_RPG/Public/Item/PickUp/RPGLootPresentationComponent.h','Source/Project_RPG/Private/Item/PickUp/RPGLootPresentationComponent.cpp','Source/Project_RPG/Private/Tests/RPGLootPresentationTests.cpp','Source/Project_RPG/Public/Item/PickUp/RPGPickUpBase.h','Source/Project_RPG/Private/Item/PickUp/RPGPickUpBase.cpp','Source/Project_RPG/Private/Ability/NPC/RPGNPCDropItemAbility.cpp')
 foreach($relative in $files){$source=Join-Path $workspace $relative;$sha=(Get-FileHash -LiteralPath $source).Hash;$target=Join-Path $temp ('Inputs/'+$relative);[IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null;Copy-Item -LiteralPath $source -Destination $target;if((Get-FileHash -LiteralPath $target).Hash -ne $sha){throw 'Input changed'};$proof.Inputs+=@{Path=$relative;SHA256=$sha}}
 $native=Join-Path $temp 'Native'
 foreach($relative in $files[0..2]){$target=Join-Path $native $relative;[IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null;Copy-Item -LiteralPath (Join-Path $temp ('Inputs/'+$relative)) -Destination $target}
 Put (Join-Path $native 'LootPresentation.uproject') '{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}],"Plugins":[{"Name":"Niagara","Enabled":true}]}'
 Put (Join-Path $native 'Source/LootPresentationEditor.Target.cs') @'
using UnrealBuildTool;
public class LootPresentationEditorTarget : TargetRules {
 public LootPresentationEditorTarget(TargetInfo Target) : base(Target) {
  Type=TargetType.Editor; DefaultBuildSettings=BuildSettingsVersion.V7; IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8; ExtraModuleNames.Add("Project_RPG");
 }
}
'@
 Put (Join-Path $native 'Source/Project_RPG/Project_RPG.Build.cs') @'
using UnrealBuildTool;
public class Project_RPG : ModuleRules {
 public Project_RPG(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.NoPCHs; bUseUnity=false;
  PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","NetCore","Niagara"});
 }
}
'@
 Put (Join-Path $native 'Source/Project_RPG/Module.cpp') '#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Project_RPG, "Project_RPG");'
 [IO.Directory]::CreateDirectory((Join-Path $native 'Content'))|Out-Null; foreach($folder in @('DrapEffet','Interface_And_Item_Sounds')){$null=New-Item -ItemType Junction -Path (Join-Path $native ('Content/'+$folder)) -Target (Join-Path $workspace ('Content/'+$folder))}
 $nativeProject=Join-Path $native 'LootPresentation.uproject'
 if(!$IntegrationOnly){
 Build 'LootPresentationEditor' $nativeProject 'NativeBuild'
 $runArgs=@(('"'+$nativeProject+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG.Item.LootPresentation"','-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot 'Native.log')+'"'),('-ReportExportPath="'+(Join-Path $temp 'Report')+'"'))
 $previousSkip=$env:UE_SKIP_UBT_SDK_SETUP
 try {$env:UE_SKIP_UBT_SDK_SETUP='1';$process=Start-Process -FilePath $editor -ArgumentList $runArgs -WindowStyle Hidden -PassThru}
 finally {if($null -eq $previousSkip){Remove-Item Env:UE_SKIP_UBT_SDK_SETUP -ErrorAction SilentlyContinue}else{$env:UE_SKIP_UBT_SDK_SETUP=$previousSkip}}
 $deadline=[DateTime]::UtcNow.AddMinutes(8)
 while(!$process.HasExited){if([DateTime]::UtcNow -gt $deadline){throw 'Native timeout'};$null=$process.WaitForExit(200)}
 $process.WaitForExit();$proof.NativeExit=$process.ExitCode
 if($process.ExitCode -ne 0){throw 'Native execution failed'}
 $report=Get-Content -LiteralPath (Join-Path $temp 'Report/index.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $proof.Tests=@($report.tests|Select-Object fullTestPath,state,errors,warnings)
 if($proof.Tests.Count -ne 4 -or @($proof.Tests|Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0 -or $_.warnings -ne 0}).Count){throw 'Native tests failed'}
 }
 $integration=Join-Path $temp 'Integration';[IO.Directory]::CreateDirectory($integration)|Out-Null
 $paths=@('Source','Config','Project_RPG.uproject')
 $pluginFiles=@(git -C $workspace ls-tree -r --name-only $proof.Head -- Plugins)
 foreach($descriptor in @($pluginFiles|Where-Object {$_ -like '*.uplugin'})){$paths+=$descriptor;$dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'));foreach($sub in @('Source','Config','Resources','Shaders')){if(@($pluginFiles|Where-Object {$_.StartsWith($dir+'/'+$sub+'/')}).Count){$paths+=($dir+'/'+$sub)}}}
 $archive=Join-Path $temp 'Published.tar'
 & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $paths
 if($LASTEXITCODE -ne 0){throw 'Archive failed'}
 & tar -xf $archive -C $integration
 if($LASTEXITCODE -ne 0){throw 'Extraction failed'}
 foreach($relative in $files){$target=Join-Path $integration $relative;[IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null;Copy-Item -LiteralPath (Join-Path $temp ('Inputs/'+$relative)) -Destination $target}
 $compileList=Join-Path $temp 'CompileFiles.txt';[IO.File]::WriteAllLines($compileList,@($files|Where-Object {$_ -like '*.cpp'}|ForEach-Object {Join-Path $integration $_}),[Text.UTF8Encoding]::new($false))
 Build 'Project_RPGEditor' (Join-Path $integration 'Project_RPG.uproject') 'IntegrationCompile' @(('-FileList='+$compileList))
 foreach($input in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $input.Path)).Hash -ne $input.SHA256){throw ('Original changed: '+$input.Path)}}
 $proof.Passed=$true
}catch{$proof.Failure=$_.Exception.Message}
finally{
 if($process -and !$process.HasExited){$process.Kill();$null=$process.WaitForExit(10000)}
 if(!$process -or $process.HasExited){if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}}
 $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
 $proof.Passed=$proof.Passed -and $proof.CleanupComplete
 Save
}
if(!$proof.Passed){throw $proof.Failure}
