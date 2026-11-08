[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [switch]$IconLifetime, [switch]$SlotLayout, [switch]$Cooldown)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[Guid]::NewGuid().ToString('N')
$suite=if($Cooldown){'ActionBarCooldown-20261109'}elseif($SlotLayout){'ActionBarLayout-20261108'}elseif($IconLifetime){'ActionBarIconLifetime-20261108'}else{'ActionBarFeedback-20261108'}
$reservation=if($Cooldown){'actionbar-cooldown-20261109'}elseif($SlotLayout){'actionbar-layout-20261108'}elseif($IconLifetime){'actionbar-icon-lifetime-20261108'}else{'actionbar-feedback-20261108'}
$root=Join-Path $workspace ('Saved/BuildChecks/'+$suite+'/'+$id)
$tempRoot=Join-Path $workspace $(if($Cooldown){'Saved/AC'}elseif($SlotLayout){'Saved/AL'}elseif($IconLifetime){'Saved/AI'}else{'Saved/AF'})
$temp=Join-Path $tempRoot $id.Substring(0,8)
$project=Join-Path $temp 'Cell'
$process=$null
$proof=[ordered]@{Passed=$false;Scope='Actual native Slate cell/layout tests, followed by published project UHT and focused CPP compile; no full game link/runtime';Head='';Inputs=@();Tests=@();Cleanup=$null;Failure=$null}
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path,[string]$Text) {
    $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
function Save-Proof { Write-Utf8 (Join-Path $root 'Result.json') ($proof|ConvertTo-Json -Depth 12) }
function Assert-Reservation {
    if(@(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md')|Where-Object{$_-match('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')}).Count-ne1){throw 'Cell reservation missing'}
}
function Build-Project([string]$Name,[string]$UProject,[string]$Phase,[string[]]$Extra=@()) {
    $arguments=@($ubt,$Name,'Win64','Development',$UProject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),'-NoHotReloadFromIDE','-MaxParallelActions=2',('-log='+(Join-Path $root ($Phase+'.log'))))+$Extra
    $proof[$Phase+'Command']=@($dotnet)+$arguments
    Save-Proof
    Write-Output ($Phase+': '+$root)
    & $dotnet @arguments *> (Join-Path $root ($Phase+'Console.log'))
    $proof[$Phase+'Exit']=$LASTEXITCODE
    if($LASTEXITCODE-ne0){throw ($Phase+' failed')}
}
try {
    Assert-Reservation
    $null=[IO.Directory]::CreateDirectory($project)
    $null=[IO.Directory]::CreateDirectory((Join-Path $project 'Content'))
    $proof.Head=(git -C $workspace rev-parse HEAD).Trim()
    $files=@('Source/Project_RPG/Public/UI/ActionBar/RPGActionBarActionView.h',
        'Source/Project_RPG/Private/UI/ActionBar/SRPGActionBarCell.h',
        'Source/Project_RPG/Private/UI/ActionBar/SRPGActionBarCell.cpp',
        'Source/Project_RPG/Private/Tests/RPGActionBarCellTests.cpp',
        'Source/Project_RPG/Public/UI/ActionBar/RPGActionBarProfile.h',
        'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarWidget.cpp',
        'Source/Project_RPG/Public/UI/ActionBar/RPGActionBarWidget.h',
        'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarLayout.h',
        'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarLayout.cpp',
        'Source/Project_RPG/Private/Tests/RPGActionBarLayoutTests.cpp',
        'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarContext.cpp',
        'Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp')
    $nativeFiles=@($files|Select-Object -First 4)+@($files[7],$files[8],$files[9])
    # Freeze the exact real product/test inputs once; both phases consume these same copies.
    $frozen=Join-Path $temp 'Inputs'
    foreach($relative in $files){
        $source=Join-Path $workspace $relative
        $destination=Join-Path $frozen $relative
        $sha=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        if((Get-FileHash -LiteralPath $source).Hash-ne$sha-or(Get-FileHash -LiteralPath $destination).Hash-ne$sha){throw 'Input changed during capture'}
        $proof.Inputs+=@{Path=$relative;SHA256=$sha}
    }
    foreach($relative in $nativeFiles){
        $destination=Join-Path $project $relative
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath (Join-Path $frozen $relative) -Destination $destination
    }
    Write-Utf8 (Join-Path $project 'ActionBarCell.uproject') @'
{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}
'@
    Write-Utf8 (Join-Path $project 'Source/ActionBarCellEditor.Target.cs') @'
using UnrealBuildTool;
public class ActionBarCellEditorTarget : TargetRules {
    public ActionBarCellEditorTarget(TargetInfo Target) : base(Target) {
        Type=TargetType.Editor; DefaultBuildSettings=BuildSettingsVersion.V7;
        IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8; ExtraModuleNames.Add("Project_RPG");
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Project_RPG.Build.cs') @'
using UnrealBuildTool;
public class Project_RPG : ModuleRules {
    public Project_RPG(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs; bUseUnity=false;
        PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","InputCore","Slate","SlateCore"});
    }
}
'@
    Write-Utf8 (Join-Path $project 'Source/Project_RPG/Private/ActionBarCellModule.cpp') @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Project_RPG, "ActionBarCell");
'@
    if(!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt'))){throw 'Installed engine required'}
    $dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $uproject=Join-Path $project 'ActionBarCell.uproject'
    Build-Project 'ActionBarCellEditor' $uproject 'CellBuild'
    $proof.ModuleSHA256=(Get-FileHash -LiteralPath (Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll')).Hash
    $query=@($ubt,'-Mode=QueryTargets',('-Project='+$uproject),('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')),'-IncludeAllTargets','-DontIncludeParentAssembly','-DontIncludeProgramTargets','-NoMutex','-SkipRulesCompile','-NoLog')
    & $dotnet @query *> (Join-Path $root 'Query.log')
    if($LASTEXITCODE-ne0){throw 'Target query failed'}
    $report=Join-Path $temp 'Report'
    $editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $arguments=@(('"'+$uproject+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG.UI.ActionBar.Cell+ProjectRPG.UI.ActionBar.Layout"','-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $root 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'))
    $proof.NativeCommand=@($editor)+$arguments
    Save-Proof
    $process=Start-Process $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $null=$process.Handle
    $deadline=[DateTime]::UtcNow.AddSeconds(300)
    while(!$process.HasExited){
        if([DateTime]::UtcNow-ge$deadline){throw 'Owned automation timed out'}
        $null=$process.WaitForExit(200)
    }
    $process.WaitForExit()
    $proof.NativeExit=$process.ExitCode
    if($process.ExitCode-ne0){throw 'Native cell automation failed'}
    $data=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $proof.Tests=@($data.tests|Select-Object fullTestPath,state,errors,warnings)
    $expected=@('DisabledTooltip','LiveFeedback','CooldownAndRelease','IconLifetime','CooldownPresentation')|ForEach-Object{'ProjectRPG.UI.ActionBar.Cell.'+$_}
    $expected+=@('Validation','Compatibility','SparseRowAndInput')|ForEach-Object{'ProjectRPG.UI.ActionBar.Layout.'+$_}
    if((Compare-Object $expected @($proof.Tests.fullTestPath))-or@($proof.Tests|Where-Object{$_.state-ne'Success'-or$_.errors-ne0-or$_.warnings-ne0}).Count){throw 'Missing or failing cell cases'}
    foreach($entry in $proof.Inputs){
        if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash-ne$entry.SHA256){throw 'Input changed after native validation'}
    }
    # The real owning widget is checked against the full published project, without linking peers or replacing shared outputs.
    Assert-Reservation
    $integration=Join-Path $temp 'Integration'
    $null=[IO.Directory]::CreateDirectory($integration)
    $paths=@('Source','Config','Project_RPG.uproject')
    $pluginFiles=@(git -C $workspace ls-tree -r --name-only $proof.Head -- Plugins)
    foreach($descriptor in @($pluginFiles|Where-Object{$_-like'*.uplugin'})){
        $paths+=$descriptor
        $dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        foreach($name in @('Source','Config','Resources','Shaders')){
            $child=$dir+'/'+$name
            if(@($pluginFiles|Where-Object{$_.StartsWith($child+'/')}).Count){$paths+=$child}
        }
    }
    & git -C $workspace archive --format=tar ('-o'+(Join-Path $temp 'Integration.tar')) $proof.Head -- $paths
    if($LASTEXITCODE-ne0){throw 'Integration archive failed'}
    & tar -xf (Join-Path $temp 'Integration.tar') -C $integration
    if($LASTEXITCODE-ne0){throw 'Integration extraction failed'}
    foreach($relative in $files){
        $destination=Join-Path $integration $relative
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath (Join-Path $frozen $relative) -Destination $destination
        [IO.File]::SetLastWriteTimeUtc($destination,[DateTime]::UtcNow)
    }
    $compileFiles=@('Source/Project_RPG/Private/UI/ActionBar/RPGActionBarWidget.cpp',
        'Source/Project_RPG/Private/UI/ActionBar/SRPGActionBarCell.cpp',
        'Source/Project_RPG/Private/Tests/RPGActionBarCellTests.cpp',
        'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarContext.cpp',
        'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarLayout.cpp',
        'Source/Project_RPG/Private/Tests/RPGActionBarLayoutTests.cpp',
        'Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp')
    $fileList=Join-Path $temp 'CompileFiles.txt'
    [IO.File]::WriteAllLines($fileList,@($compileFiles|ForEach-Object{Join-Path $integration $_}),[Text.UTF8Encoding]::new($false))
    Build-Project 'Project_RPGEditor' (Join-Path $integration 'Project_RPG.uproject') 'IntegrationBuild' @(('-FileList='+$fileList))
    foreach($entry in $proof.Inputs){
        if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash-ne$entry.SHA256){throw 'Input changed after validation'}
    }
    $proof.Passed=$true
} catch {$proof.Failure=$_.Exception.Message}
finally {
    $released=$true
    if($process){try{if(!$process.HasExited){$process.Kill()};if(!$process.WaitForExit(10000)){throw 'Owned process still active'}}catch{$released=$false;$proof.Failure=$_.Exception.Message}}
    try {
        Assert-Reservation
        if(!$released){throw 'Cannot clean active runtime'}
        if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}
    }catch{$proof.Failure=$_.Exception.Message}
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
    $proof.Passed=$proof.Passed-and!$proof.Failure-and$proof.CleanupComplete
    $proof.HistoricalHashesOnly=$true
    Save-Proof
    Write-Output ('Passed='+$proof.Passed+'; proof='+$root)
}
if(!$proof.Passed){throw $proof.Failure}
