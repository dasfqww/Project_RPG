[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [int]$RepairWindowSeconds=600,
    [switch]$IncludeRidingBridge, [switch]$IncludeIdentity,
    [ValidateRange(1,4)][int]$MaxParallelActions=2)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[Guid]::NewGuid().ToString('N')
if($IncludeIdentity){$IncludeRidingBridge=$true}
$suite=if($IncludeIdentity){'ActionBarIdentity-20261108'}elseif($IncludeRidingBridge){'ActionBarRiding-20261108'}else{'ActionBarContext-20261106'}
$reservation=if($IncludeIdentity){'actionbar-identity-20261108'}elseif($IncludeRidingBridge){'actionbar-riding-20261108'}else{'actionbar-context-20261106'}
$root=Join-Path $workspace ('Saved/BuildChecks/'+$suite+'/'+$id)
$tempRoot=Join-Path $workspace 'Saved/AB'
$temp=Join-Path $tempRoot $id.Substring(0,8)
$project=Join-Path $temp 'Project'
$process=$null
$failure=$null
$proof=[ordered]@{Passed=$false;Head='';Overlay=@();Tests=@();Attempts=@();Cleanup=$null}
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Save-Proof { [IO.File]::WriteAllText((Join-Path $root 'Result.json'),($proof|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false)) }
function Copy-Overlay {
    $proof.Overlay=@()
    foreach($relative in $overlay){
        $source=Join-Path $workspace $relative
        $destination=Join-Path $project $relative
        $hash=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
        if(!(Test-Path -LiteralPath $destination)-or(Get-FileHash -LiteralPath $destination).Hash-ne$hash){
            Copy-Item -LiteralPath $source -Destination $destination
            [IO.File]::SetLastWriteTimeUtc($destination,[DateTime]::UtcNow)
        }
        if((Get-FileHash -LiteralPath $source).Hash-ne$hash-or(Get-FileHash -LiteralPath $destination).Hash-ne$hash){throw 'Overlay changed during capture'}
        $proof.Overlay+=@{Path=$relative;SHA256=$hash}
    }
}
try {
    $rows=@(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md')|Where-Object{$_-match('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')})
    if($rows.Count-ne1){throw 'Missing actionbar reservation'}
    $null=[IO.Directory]::CreateDirectory($root)
    $null=[IO.Directory]::CreateDirectory($project)
    $proof.Head=(git -C $workspace rev-parse HEAD).Trim()
    $paths=[Collections.Generic.List[string]]::new()
    foreach($p in @('Source','Config','Project_RPG.uproject')){$paths.Add($p)}
    $pluginFiles=@(git -C $workspace ls-tree -r --name-only $proof.Head -- Plugins)
    $descriptors=@($pluginFiles|Where-Object{$_-like'*.uplugin'})
    foreach($descriptor in $descriptors){
        $paths.Add($descriptor)
        $dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        foreach($name in @('Source','Config','Resources','Shaders')){
            $child=$dir+'/'+$name
            if(@($pluginFiles|Where-Object{$_.StartsWith($child+'/')}).Count){$paths.Add($child)}
        }
    }
    $archive=Join-Path $temp 'Inputs.tar'
    & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $paths.ToArray()
    if($LASTEXITCODE-ne0){throw 'Archive failed'}
    & tar -xf $archive -C $project
    if($LASTEXITCODE-ne0){throw 'Extraction failed'}
    # Identity validation overlays only this feature's owned inputs. Common callers
    # and existing tests stay at the archived HEAD, including after a repair retry.
    if($IncludeIdentity){
        $overlay=@(
            'Source/Project_RPG/Public/UI/ActionBar/RPGActionBarActionView.h',
            'Source/Project_RPG/Public/UI/ActionBar/RPGActionBarProvider.h',
            'Source/Project_RPG/Public/UI/ActionBar/RPGActionBarContext.h',
            'Source/Project_RPG/Public/UI/ActionBar/RPGActionBarWidget.h',
            'Source/Project_RPG/Public/UI/ActionBar/RPGActionBarIdentityWidget.h',
            'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarContext.cpp',
            'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarWidget.cpp',
            'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarIdentityWidget.cpp',
            'Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp',
            'Source/Project_RPG/Private/Tests/RPGActionBarContextTests.cpp',
            'Source/Project_RPG/Private/Tests/RPGRidingActionBarTests.cpp'
        )
    }else{
        $overlay=@(
            'Source/Project_RPG/Public/Component/UI/QuickSlotComponent.h',
            'Source/Project_RPG/Private/Component/UI/QuickSlotComponent.cpp',
            'Source/Project_RPG/Public/UI/HUD/RPGHUD.h',
            'Source/Project_RPG/Private/UI/HUD/RPGHUD.cpp',
            'Source/Project_RPG/Private/Controller/RPGPlayerController.cpp',
            'Source/Project_RPG/Private/Tests/RPGActionBarContextTests.cpp',
            'Source/Project_RPG/Private/Tests/RPGActionBarCellTests.cpp'
        )
        if($IncludeRidingBridge){
            $overlay+=@('Source/Project_RPG/Private/Tests/RPGRidingActionBarTests.h','Source/Project_RPG/Private/Tests/RPGRidingActionBarTests.cpp')
        }
        foreach($kind in @('Public','Private')){
            $folder=Join-Path $workspace ('Source/Project_RPG/'+$kind+'/UI/ActionBar')
            foreach($file in Get-ChildItem -LiteralPath $folder -File){
                $overlay+=$file.FullName.Substring($workspace.Length+1).Replace('\','/')
            }
        }
    }
    Copy-Overlay
    $null=New-Item -ItemType Junction -Path (Join-Path $project 'Content') -Target (Join-Path $workspace 'Content')
    foreach($descriptor in $descriptors){
        $dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        $content=Join-Path $workspace ($dir+'/Content')
        if(Test-Path -LiteralPath $content){$null=New-Item -ItemType Junction -Path (Join-Path $project ($dir+'/Content')) -Target $content}
    }
    $config=Join-Path $project 'Saved/UnrealBuildTool/BuildConfiguration.xml'
    $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $config))
    [IO.File]::WriteAllText($config,'<?xml version="1.0" encoding="utf-8"?><Configuration xmlns="https://www.unrealengine.com/BuildConfiguration"><ParallelExecutor><bStopCompilationAfterErrors>true</bStopCompilationAfterErrors></ParallelExecutor></Configuration>',[Text.UTF8Encoding]::new($false))
    $uproject=Join-Path $project 'Project_RPG.uproject'
    $attempt=0
    while($true){
        ++$attempt
        try {
            if(!(Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Build/InstalledBuild.txt'))){throw 'Installed engine required'}
            $build=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
            $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
            # Project rules, UHT, objects and UBA cache are private. UBT may still write its shared diagnostic trace.
            $args=@($ubt,'Project_RPGEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),('-UBATraceOutputFile='+(Join-Path $temp ('Build-'+$attempt+'.uba'))),'-NoHotReloadFromIDE',('-MaxParallelActions='+$MaxParallelActions),('-log='+(Join-Path $root ('Build-'+$attempt+'.log'))))
            if($IncludeIdentity){
                $list=Join-Path $temp 'IdentityCompileFiles.txt'
                $compile=@('Source/Project_RPG/Private/UI/ActionBar/RPGActionBarContext.cpp',
                    'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarWidget.cpp',
                    'Source/Project_RPG/Private/UI/ActionBar/RPGActionBarIdentityWidget.cpp',
                    'Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp',
                    'Source/Project_RPG/Private/Tests/RPGActionBarContextTests.cpp',
                    'Source/Project_RPG/Private/Tests/RPGRidingActionBarTests.cpp')
                [IO.File]::WriteAllLines($list,@($compile|ForEach-Object{Join-Path $project $_}),[Text.UTF8Encoding]::new($false))
                $preflight=@($args|Where-Object{!$_.StartsWith('-log=')})+@(('-FileList='+$list),('-log='+(Join-Path $root ('Preflight-'+$attempt+'.log'))))
                $proof.PreflightCommand=@($build)+$preflight
                Save-Proof
                Write-Output ('Compiling identity inputs '+$root+' attempt '+$attempt)
                & $build @preflight *> (Join-Path $root ('PreflightConsole-'+$attempt+'.log'))
                $proof.PreflightExit=$LASTEXITCODE
                if($LASTEXITCODE-ne0){throw 'Identity input compile failed'}
            }
            $proof.BuildCommand=@($build)+$args
            Save-Proof
            Write-Output ('Building '+$root+' attempt '+$attempt)
            & $build @args *> (Join-Path $root ('BuildConsole-'+$attempt+'.log'))
            $proof.BuildExit=$LASTEXITCODE
            if($LASTEXITCODE-ne0){throw 'Independent Editor build failed'}
            $module=Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
            $proof.ModuleSHA256=(Get-FileHash -LiteralPath $module).Hash
            $dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
            $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
            & $dotnet $ubt '-Mode=QueryTargets' ('-Project='+$uproject) ('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')) '-IncludeAllTargets' '-DontIncludeParentAssembly' '-DontIncludeProgramTargets' '-NoMutex' '-SkipRulesCompile' '-NoLog' *> (Join-Path $root ('Query-'+$attempt+'.log'))
            if($LASTEXITCODE-ne0){throw 'Target query failed'}
            $report=Join-Path $temp ('Report-'+$attempt)
            $editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
            $tests='ProjectRPG.UI.ActionBar+ProjectRPG.Skill.QuickSlot+ProjectRPG.UI.PlayerHUD'
            $args=@(('"'+$uproject+'"'),'/Engine/Maps/Entry',('-ExecCmds="Automation RunTests '+$tests+'"'),'-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),'-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $root ('Native-'+$attempt+'.log'))+'"'),('-ReportExportPath="'+$report+'"'))
            $proof.NativeCommand=@($editor)+$args
            Save-Proof
            $process=Start-Process $editor -ArgumentList $args -WindowStyle Hidden -PassThru
            $null=$process.Handle
            $deadline=[DateTime]::UtcNow.AddSeconds(600)
            while(!$process.HasExited){
                if([DateTime]::UtcNow-ge$deadline){throw 'Owned automation timed out'}
                $null=$process.WaitForExit(200)
            }
            $process.WaitForExit()
            $proof.NativeExit=$process.ExitCode
            if($process.ExitCode-ne0){throw 'Native automation failed'}
            $data=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8|ConvertFrom-Json
            $proof.Tests=@($data.tests|Select-Object fullTestPath,state,errors,warnings)
            if(@($proof.Tests|Where-Object{$_.fullTestPath-like'ProjectRPG.UI.ActionBar.*'}).Count-lt7-or$proof.Tests.Count-lt8-or@($proof.Tests|Where-Object state -ne 'Success').Count){throw 'Missing or failing automation cases'}
            $expected=@()
            if($IncludeRidingBridge){$expected+=@('OwnershipAndCleanup','PossessionAndHUDLifetime','TransitionsAndCommand')|ForEach-Object{'ProjectRPG.UI.ActionBar.Riding.'+$_}}
            if($IncludeIdentity){$expected+=@('ProjectRPG.UI.ActionBar.Riding.IdentityAndMovement','ProjectRPG.UI.ActionBar.IdentityReadBoundary')}
            if(@($expected|Where-Object{$_-notin$proof.Tests.fullTestPath}).Count){throw 'Missing Riding/identity cases'}
            foreach($entry in $proof.Overlay){
                if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash-ne$entry.SHA256){throw 'Source changed after validation'}
            }
            $proof.Passed=$true
            break
        } catch {
            $proof.Attempts+=@{Attempt=$attempt;Failure=$_.Exception.Message}
            $proof.RepairUntilUtc=[DateTime]::UtcNow.AddSeconds($RepairWindowSeconds).ToString('o')
            Save-Proof
            Write-Output ('Attempt failed: '+$_.Exception.Message+'; repair marker: '+(Join-Path $root 'Retry.json'))
            if($process-and!$process.HasExited){$process.Kill();$null=$process.WaitForExit(10000)}
            $retry=Join-Path $root 'Retry.json'
            while(!(Test-Path -LiteralPath $retry)){
                if([DateTime]::UtcNow-ge[DateTime]::Parse($proof.RepairUntilUtc)){throw 'Repair window expired'}
                Start-Sleep -Milliseconds 200
            }
            $marker=Get-Content -LiteralPath $retry -Raw|ConvertFrom-Json
            if($marker.Attempt-ne$attempt){throw 'Stale retry marker'}
            Remove-Item -LiteralPath $retry
            Copy-Overlay
        }
    }
} catch {$failure=$_.Exception.Message}
finally {
    $released=$true
    if($process){try{if(!$process.HasExited){$process.Kill()};if(!$process.WaitForExit(10000)){throw 'Owned process still active'}}catch{$released=$false;$failure=$_.Exception.Message}}
    $rows=@(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md')|Where-Object{$_-match('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')})
    if($rows.Count-ne1){$released=$false;$failure='Reservation missing at cleanup'}
    if($released-and(Test-Path -LiteralPath $temp)){
        try{$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}catch{$failure=$_.Exception.Message}
    }
    $proof.Failure=$failure
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
    $proof.Passed=$proof.Passed-and!$failure-and$proof.CleanupComplete
    $proof.HistoricalHashesOnly=$true
    if(Test-Path -LiteralPath $root){Save-Proof}
    Write-Output ('Passed='+$proof.Passed+'; Proof='+$root)
}
if(!$proof.Passed){throw $failure}
