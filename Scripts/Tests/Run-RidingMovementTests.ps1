[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [ValidateRange(0,120)][int]$RepairWindowSeconds=120)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run=[Guid]::NewGuid().ToString('N')
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingMovement-20261108/'+$run)
$tempRoot=Join-Path $workspace 'Saved/RM'
$temp=Join-Path $tempRoot $run.Substring(0,8)
$project=Join-Path $temp 'Project'
$proof=[ordered]@{Passed=$false;Head='';Inputs=@();Attempts=@();Cleanup=$null;Failure=$null}
$process=$null
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Write-Utf8([string]$Path,[string]$Value){$null=[IO.Directory]::CreateDirectory((Split-Path $Path));[IO.File]::WriteAllText($Path,$Value,[Text.UTF8Encoding]::new($false))}
function Save-Proof {Write-Utf8 (Join-Path $proofRoot 'Result.json') ($proof|ConvertTo-Json -Depth 10)}
function Assert-Reservation {
    if(@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object{$_-match'^\|[^|]* / riding-movement-20261108 \|'}).Count-ne1){throw 'Movement reservation missing'}
}
$files=@(
    'Source/Project_RPG/Public/Riding/RPGRidingTypes.h','Source/Project_RPG/Private/Riding/RPGRidingTypes.cpp',
    'Source/Project_RPG/Public/Riding/RPGRidingComponent.h','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp',
    'Source/Project_RPG/Private/Riding/RPGRidingSession.h','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp',
    'Source/Project_RPG/Public/Riding/RPGRidingMovementPrediction.h','Source/Project_RPG/Private/Riding/RPGRidingMovementPrediction.cpp',
    'Source/Project_RPG/Public/Component/RPGCharacterMovementComponent.h','Source/Project_RPG/Private/Component/RPGCharacterMovementComponent.cpp',
    'Source/Project_RPG/Public/Component/RPGPlayerMovementComponent.h','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp',
    'Source/Project_RPG/Private/Tests/RPGRidingFoundationTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingSessionTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGRidingActorTests.h','Source/Project_RPG/Private/Tests/RPGRidingActorTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGRidingPresentationTests.cpp',
    'Source/Project_RPG/Private/Tests/RPGRidingMovementTests.h','Source/Project_RPG/Private/Tests/RPGRidingMovementTests.cpp')
function Capture-Overlay {
    $proof.Inputs=@()
    foreach($relative in $files){
        $source=Join-Path $workspace $relative;$destination=Join-Path $project $relative
        $sha=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path $destination))
        if(!(Test-Path -LiteralPath $destination)-or(Get-FileHash -LiteralPath $destination).Hash-ne$sha){
            Copy-Item -LiteralPath $source -Destination $destination
            [IO.File]::SetLastWriteTimeUtc($destination,[DateTime]::UtcNow)
        }
        if((Get-FileHash -LiteralPath $source).Hash-ne$sha-or(Get-FileHash -LiteralPath $destination).Hash-ne$sha){throw 'Overlay changed during capture'}
        $proof.Inputs+=@{Path=$relative;SHA256=$sha}
    }
}
try {
    Assert-Reservation
    $null=[IO.Directory]::CreateDirectory($project)
    $null=[IO.Directory]::CreateDirectory($proofRoot)
    $proof.Head=(git -C $workspace rev-parse HEAD).Trim()
    $paths=@('Source','Config','Project_RPG.uproject')
    $pluginFiles=@(git -C $workspace ls-tree -r --name-only HEAD -- Plugins)
    $descriptors=@($pluginFiles|Where-Object {$_-like'*.uplugin'})
    foreach($descriptor in $descriptors){
        $paths+=$descriptor;$dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        foreach($name in @('Source','Config','Resources','Shaders')){$child=$dir+'/'+$name;if(@($pluginFiles|Where-Object {$_.StartsWith($child+'/')}).Count){$paths+=$child}}
    }
    & git -C $workspace archive --format=tar ('-o'+(Join-Path $temp 'Inputs.tar')) $proof.Head -- $paths
    if($LASTEXITCODE-ne0){throw 'Source archive failed'}
    & tar -xf (Join-Path $temp 'Inputs.tar') -C $project
    if($LASTEXITCODE-ne0){throw 'Source extraction failed'}
    Capture-Overlay
    $null=New-Item -ItemType Junction -Path (Join-Path $project 'Content') -Target (Join-Path $workspace 'Content')
    foreach($descriptor in $descriptors){$dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'));$content=Join-Path $workspace ($dir+'/Content');if(Test-Path -LiteralPath $content){$null=New-Item -ItemType Junction -Path (Join-Path $project ($dir+'/Content')) -Target $content}}
    $dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $uproject=Join-Path $project 'Project_RPG.uproject'
    for($attempt=1;;$attempt++){
        $entry=[ordered]@{Attempt=$attempt;BuildExit=$null;NativeExit=$null;Failure=$null}
        try {
            Assert-Reservation
            $buildArgs=@($ubt,'Project_RPGEditor','Win64','Development',$uproject,'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),'-NoHotReloadFromIDE','-MaxParallelActions=2',('-log='+(Join-Path $proofRoot ('Build-'+$attempt+'.log'))))
            $entry.BuildCommand=@($dotnet)+$buildArgs
            Save-Proof
            Write-Output ('Building full Riding movement snapshot; attempt '+$attempt+'; proof: '+$proofRoot)
            & $dotnet @buildArgs *> (Join-Path $proofRoot ('BuildConsole-'+$attempt+'.log'))
            $entry.BuildExit=$LASTEXITCODE
            if($LASTEXITCODE-ne0){throw 'Full Editor build failed'}
            $module=Join-Path $project 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
            $proof.ModuleSHA256=(Get-FileHash -LiteralPath $module).Hash
            & $dotnet $ubt '-Mode=QueryTargets' ('-Project='+$uproject) ('-Output='+(Join-Path $project 'Intermediate/TargetInfo.json')) '-IncludeAllTargets' '-DontIncludeParentAssembly' '-DontIncludeProgramTargets' '-NoMutex' '-SkipRulesCompile' '-NoLog' *> (Join-Path $proofRoot ('Query-'+$attempt+'.log'))
            if($LASTEXITCODE-ne0){throw 'Private target query failed'}
            $report=Join-Path $temp ('Report'+$attempt)
            $tests='ProjectRPG.Riding.Foundation+ProjectRPG.Riding.Session+ProjectRPG.Riding.Actors+ProjectRPG.Riding.Presentation+ProjectRPG.Riding.Movement+ProjectRPG.Combat.Slow+ProjectRPG.Combat.Control+ProjectRPG.Online.DungeonAuthorityRevocation.Gameplay'
            $nativeArgs=@(('"'+$uproject+'"'),'/Engine/Maps/Entry',('-ExecCmds="Automation RunTests '+$tests+'"'),'-TestExit="Automation Test Queue Empty"','-unattended','-nullrhi','-nosound','-nosteam','-nop4','-nosplash','-NoLiveCoding','-DisablePython','-NoLoadingScreen','-Multiprocess','-DDC=(InstalledEnginePak,Local)',('-LocalDataCachePath="'+(Join-Path $temp 'Cache')+'"'),('-ShaderWorkingDir="'+(Join-Path $temp 'Shaders')+'"'),'-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',('-UserDir="'+(Join-Path $temp 'User')+'"'),('-abslog="'+(Join-Path $proofRoot ('Native-'+$attempt+'.log'))+'"'),('-ReportExportPath="'+$report+'"'))
            $entry.NativeCommand=$nativeArgs
            $process=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $nativeArgs -WindowStyle Hidden -PassThru
            $null=$process.Handle;$deadline=[DateTime]::UtcNow.AddSeconds(600)
            while(!$process.HasExited){if([DateTime]::UtcNow-ge$deadline){throw 'Owned native process timed out'};$null=$process.WaitForExit(200)}
            $process.WaitForExit();$entry.NativeExit=$process.ExitCode
            if($process.ExitCode-ne0){throw 'Native movement process failed'}
            $results=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw -Encoding UTF8|ConvertFrom-Json
            $proof.Tests=@($results.tests|Select-Object fullTestPath,state,errors,warnings)
            $expected=@('Profile','SavedMoves','Wire','Correction','ServerGuards')|ForEach-Object {'ProjectRPG.Riding.Movement.'+$_}
            if(@(Compare-Object $expected @($proof.Tests|Where-Object fullTestPath -like 'ProjectRPG.Riding.Movement.*'|ForEach-Object fullTestPath)).Count-or$proof.Tests.Count-lt37-or@($proof.Tests|Where-Object {$_.state-ne'Success'-or$_.errors-ne0}).Count){throw 'Missing/failing movement or affected regression cases'}
            foreach($inputFile in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash-ne$inputFile.SHA256){throw 'Original source changed during validation'}}
            $proof.Passed=$true
        } catch {$entry.Failure=$_.Exception.Message}
        $proof.Attempts+=,$entry
        if($process-and!$process.HasExited){$process.Kill();$null=$process.WaitForExit(10000)}
        Save-Proof
        if($proof.Passed){break}
        $marker=Join-Path $proofRoot 'Retry.request'
        Write-Output ('Validation failed: '+$entry.Failure+'; bounded repair: '+$marker)
        $repairUntil=[DateTime]::UtcNow.AddSeconds($RepairWindowSeconds)
        while(!(Test-Path -LiteralPath $marker)-and[DateTime]::UtcNow-lt$repairUntil){Start-Sleep -Milliseconds 200}
        if(!(Test-Path -LiteralPath $marker)){throw $entry.Failure}
        # A single explicit marker owns one retry. No time-based automatic rebuild.
        Move-Item -LiteralPath $marker -Destination (Join-Path $proofRoot ('Retry-'+$attempt+'.consumed'))
        Capture-Overlay
    }
} catch {$proof.Failure=$_.Exception.Message}
finally {
    try {
        if($process-and!$process.HasExited){$process.Kill();if(!$process.WaitForExit(10000)){throw 'Owned runtime still active'}}
        Assert-Reservation
        if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}
    } catch {$proof.Failure=$_.Exception.Message}
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
    $proof.HistoricalHashesOnly=$true
    $proof.Passed=$proof.Passed-and!$proof.Failure-and$proof.CleanupComplete
    Save-Proof
    Write-Output ('Passed='+$proof.Passed+'; proof='+$proofRoot)
}
if(!$proof.Passed){throw $proof.Failure}
