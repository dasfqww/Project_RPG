[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [string]$BaseCommit='HEAD', [switch]$IntegrationOnly)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run=[Guid]::NewGuid().ToString('N')
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/NarrativeWindowLayout-20261114/'+$run)
$tempRoot=Join-Path $workspace 'Saved/NWL'
$temp=Join-Path $tempRoot $run.Substring(0,8)
$proof=[ordered]@{Passed=$false;Scope='Actual shared UMG frame/Slate layout with4096-limit long text, pinned footer, small viewport/DPI resize and clipping predicates; full project UHT and actual helper/story/test CPP3. No whole game link, real story viewport rendering/physical input, HTTP or Steam proof.';Inputs=@();Failure=$null;Cleanup=$null}
$process=$null
$ownsTemp=$false
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path,[string]$Text) {
    $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
function Save-Proof { Write-Utf8 (Join-Path $proofRoot 'Result.json') ($proof|ConvertTo-Json -Depth 10) }
function Assert-Reservation {
    $rows=@(Get-Content (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match '^\|[^|]* / narrative-window-layout-20261114 \|'})
    if($rows.Count -ne 1){throw 'Investigation reservation missing'}
}
function Copy-Input([string]$Relative,[string]$Root) {
    $source=Join-Path (Join-Path $temp 'Inputs') $Relative
    $destination=Join-Path $Root $Relative
    $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
    Copy-Item -LiteralPath $source -Destination $destination
    [IO.File]::SetLastWriteTimeUtc($destination,[DateTime]::UtcNow)
}
function Build([string]$Target,[string]$Project,[string]$Label,[string[]]$Extra=@()) {
    $dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $buildArgs=@($ubt,$Target,'Win64','Development',$Project,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',
        ('-UBARootDir='+(Join-Path $temp 'Uba')),('-UBATraceOutputFile='+(Join-Path $temp ($Label+'.uba'))),
        '-NoHotReloadFromIDE','-MaxParallelActions=1',('-log='+(Join-Path $proofRoot ($Label+'.log'))))+$Extra
    $proof[$Label+'Command']=@($dotnet)+$buildArgs; Save-Proof
    Write-Output ('Starting '+$Label+'; proof='+$proofRoot)
    & $dotnet @buildArgs *> (Join-Path $proofRoot ($Label+'Console.log'))
    $proof[$Label+'Exit']=$LASTEXITCODE
    if($LASTEXITCODE -ne 0){throw ($Label+' failed')}
}
try {
    Assert-Reservation
    if(Test-Path -LiteralPath $temp){throw 'Temporary path collision'}
    $null=[IO.Directory]::CreateDirectory($temp); $ownsTemp=$true
    $proof.Head=(git -C $workspace rev-parse $BaseCommit).Trim()
    if($LASTEXITCODE -ne 0){throw 'Invalid immutable base'}
    $proof.RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash
    $files=@(
        'Source/Project_RPG/Public/UI/Narrative/RPGNarrativeWindowLayout.h',
        'Source/Project_RPG/Private/UI/Narrative/RPGNarrativeWindowLayout.cpp',
        'Source/Project_RPG/Private/Tests/RPGNarrativeWindowLayoutTests.cpp',
        'Source/Project_RPG/Public/UI/Narrative/RPGNarrativeDialogueWidget.h',
        'Source/Project_RPG/Private/UI/Narrative/RPGNarrativeDialogueWidget.cpp')
    foreach($relative in $files) {
        $source=Join-Path $workspace $relative; $destination=Join-Path (Join-Path $temp 'Inputs') $relative
        $sha=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        if((Get-FileHash -LiteralPath $source).Hash -ne $sha -or (Get-FileHash -LiteralPath $destination).Hash -ne $sha){throw 'Input changed during capture'}
        $proof.Inputs+=@{Path=$relative;SHA256=$sha}
    }
    if (!$IntegrationOnly) {
    $native=Join-Path $temp 'Native'
    foreach($relative in @(
        'Source/Project_RPG/Public/UI/Narrative/RPGNarrativeWindowLayout.h',
        'Source/Project_RPG/Private/UI/Narrative/RPGNarrativeWindowLayout.cpp',
        'Source/Project_RPG/Private/Tests/RPGNarrativeWindowLayoutTests.cpp')){Copy-Input $relative $native}
    $null=[IO.Directory]::CreateDirectory((Join-Path $native 'Content'))
    Write-Utf8 (Join-Path $native 'NarrativeWindowLayout.uproject') @'
{"FileVersion":3,"EngineAssociation":"5.8","DisableEnginePluginsByDefault":true,"Modules":[{"Name":"Project_RPG","Type":"Runtime","LoadingPhase":"Default"}]}
'@
    Write-Utf8 (Join-Path $native 'Source/NarrativeWindowLayoutEditor.Target.cs') @'
using UnrealBuildTool;
public class NarrativeWindowLayoutEditorTarget : TargetRules {
    public NarrativeWindowLayoutEditorTarget(TargetInfo Target) : base(Target) {
        Type=TargetType.Editor; DefaultBuildSettings=BuildSettingsVersion.V7;
        IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8; ExtraModuleNames.Add("Project_RPG");
    }
}
'@
    Write-Utf8 (Join-Path $native 'Source/Project_RPG/Project_RPG.Build.cs') @'
using UnrealBuildTool;
public class Project_RPG : ModuleRules {
    public Project_RPG(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage=PCHUsageMode.NoPCHs; bUseUnity=false;
        PublicDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","Json","HTTP","UMG","Slate","SlateCore"});
    }
}
'@
    Write-Utf8 (Join-Path $native 'Source/Project_RPG/Private/NarrativeWindowLayoutModule.cpp') @'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Project_RPG, "NarrativeWindowLayout");
'@
    $nativeProject=Join-Path $native 'NarrativeWindowLayout.uproject'
    Build 'NarrativeWindowLayoutEditor' $nativeProject 'NativeBuild'
    $proof.ModuleSHA256=(Get-FileHash -LiteralPath (Join-Path $native 'Binaries/Win64/UnrealEditor-Project_RPG.dll')).Hash
    # Precompute through the direct UBT entrypoint; editor startup otherwise invokes shared Build.bat.
    $queryDotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $queryUbt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    & $queryDotnet $queryUbt '-Mode=QueryTargets' ('-Project='+$nativeProject) ('-Output='+(Join-Path $native 'Intermediate/TargetInfo.json')) '-IncludeAllTargets' '-DontIncludeParentAssembly' '-DontIncludeProgramTargets' '-NoMutex' '-SkipRulesCompile' '-NoLog' *> (Join-Path $proofRoot 'Query.log')
    $proof.QueryExit=$LASTEXITCODE
    if($LASTEXITCODE -ne 0){throw 'Private target query failed'}
    $report=Join-Path $temp 'Report'
    $editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $editorArgs=@(('"'+$nativeProject+'"'),'/Engine/Maps/Entry','-ExecCmds="Automation RunTests ProjectRPG.Narrative.WindowLayout"',
        '-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding',
        '-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',
        ('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),
        ('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot 'Native.log')+'"'),('-ReportExportPath="'+$report+'"'))
    $previousSkip=$env:UE_SKIP_UBT_SDK_SETUP
    try {$env:UE_SKIP_UBT_SDK_SETUP='1'; $process=Start-Process -FilePath $editor -ArgumentList $editorArgs -WindowStyle Hidden -PassThru}
    finally {if($null -eq $previousSkip){Remove-Item Env:UE_SKIP_UBT_SDK_SETUP -ErrorAction SilentlyContinue}else{$env:UE_SKIP_UBT_SDK_SETUP=$previousSkip}}
    $deadline=[DateTime]::UtcNow.AddSeconds(600)
    while(!$process.HasExited){if([DateTime]::UtcNow -ge $deadline){throw 'Owned native test timed out'}; $null=$process.WaitForExit(200)}
    $process.WaitForExit(); $proof.NativeExit=$process.ExitCode
    if($process.ExitCode -ne 0){throw 'Native automation process failed'}
    $result=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $proof.Tests=@($result.tests|Select-Object fullTestPath,state,errors,warnings)
    $expected=@('Bounds','LongContent','Resize','Visibility')|ForEach-Object {'ProjectRPG.Narrative.WindowLayout.'+$_}
    if((Compare-Object $expected @($proof.Tests.fullTestPath)) -or @($proof.Tests|Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0 -or $_.warnings -ne 0}).Count){throw 'Missing or failing native tests'}
    }
    Assert-Reservation
    $integration=Join-Path $temp 'Integration'; $null=[IO.Directory]::CreateDirectory($integration)
    $paths=@('Source','Config','Project_RPG.uproject')
    $pluginFiles=@(git -C $workspace ls-tree -r --name-only $proof.Head -- Plugins)
    foreach($descriptor in @($pluginFiles|Where-Object {$_ -like '*.uplugin'})) {
        $paths+=$descriptor; $dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        foreach($sub in @('Source','Config','Resources','Shaders')) {
            if(@($pluginFiles|Where-Object {$_ -like ($dir+'/'+$sub+'/*')}).Count){$paths+=($dir+'/'+$sub)}
        }
    }
    $archive=Join-Path $temp 'Published.tar'
    & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $paths
    if($LASTEXITCODE -ne 0){throw 'Immutable source archive failed'}
    & tar -xf $archive -C $integration
    if($LASTEXITCODE -ne 0){throw 'Immutable source extraction failed'}
    foreach($relative in $files){Copy-Input $relative $integration}
    $fileList=Join-Path $temp 'CompileFiles.txt'
    [IO.File]::WriteAllLines($fileList,@($files|Where-Object {$_ -like '*.cpp'}|ForEach-Object {Join-Path $integration $_}),[Text.UTF8Encoding]::new($false))
    Build 'Project_RPGEditor' (Join-Path $integration 'Project_RPG.uproject') 'IntegrationCompile' @(('-FileList='+$fileList))
    foreach($inputFile in $proof.Inputs) {
        if((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash -ne $inputFile.SHA256){throw 'Original input changed after capture'}
    }
    if((Get-FileHash -LiteralPath $PSCommandPath).Hash -ne $proof.RunnerSHA256){throw 'Runner changed during validation'}
    $proof.Passed=$true
} catch {$proof.Failure=$_.Exception.Message}
finally {
    $released=$true
    if($process) {
        try {if(!$process.HasExited){$process.Kill()}; if(!$process.WaitForExit(10000)){throw 'Owned native process still alive'}}
        catch {$released=$false;$proof.Failure=$_.Exception.Message}
    }
    try {
        Assert-Reservation
        if(!$released){throw 'Cannot clean an active owned runtime'}
        if($ownsTemp -and (Test-Path -LiteralPath $temp)){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}
    } catch {$proof.Failure=$_.Exception.Message}
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
    $proof.HistoricalHashesOnly=$true
    $proof.Passed=$proof.Passed -and !$proof.Failure -and $proof.CleanupComplete
    Save-Proof
    Write-Output ('Passed='+$proof.Passed+'; proof='+$proofRoot)
}
if(!$proof.Passed){throw $proof.Failure}
