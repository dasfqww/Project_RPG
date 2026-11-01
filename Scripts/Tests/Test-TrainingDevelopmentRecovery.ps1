$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'TrainingDevelopmentRecovery.ps1')
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$proofRoot=Join-Path $workspace 'Saved/BuildChecks/TrainingInterruptionRecovery-20261031'
$null=New-Item -ItemType Directory -Path $proofRoot -Force
$runs=[Collections.Generic.List[string]]::new();$owned=[Collections.Generic.List[object]]::new()
$proof=[ordered]@{passed=$false;groups=@();deletedBytes=0L;failure=$null;cleanupExceptions=@();ueStarted=$false;projectBackendBuilt=$false}
function New-FixtureRun {
    $root=Join-Path $workspace ('Saved/TrainingDevelopment/'+[Guid]::NewGuid().ToString('N'))
    $null=New-Item -ItemType Directory -Path (Join-Path $root 'Temp') -Force
    $runs.Add($root);return $root
}
function Track($Process){$null=$Process.Handle;$owned.Add(@{process=$Process;identity=(Get-RPGTrainingProcessIdentity $Process)});return $Process}
function Need([bool]$Condition,[string]$Message){if(!$Condition){throw $Message}}
function Reject([scriptblock]$Action,[string]$Pattern){$denied=$false;try{& $Action|Out-Null}catch{if($_.Exception.Message -match $Pattern){$denied=$true}else{throw}};Need $denied ('Expected rejection: '+$Pattern)}
function Prepare-Child([string]$Root){
    $dest=Join-Path $Root 'Temp/Build';$null=New-Item -ItemType Directory -Path $dest -Force
    foreach($file in Get-ChildItem -LiteralPath $script:fixtureBinary -File){Copy-Item -LiteralPath $file.FullName -Destination $dest}
    $p=Track (Start-Process $script:dotnet -ArgumentList ('"'+(Join-Path $dest 'ProjectRpg.Backend.dll')+'"') -PassThru -WindowStyle Hidden)
    return $p
}
function Orphan-Record([string]$Root,$Child){
    $r=New-RPGTrainingLifecycle $Root 'D:/UE_5.8';$r.parent=$script:deadParent
    if($Child){$r.children=@((Get-RPGTrainingProcessIdentity $Child))}
    Write-RPGTrainingLifecycle (Join-Path $Root 'Lifecycle.json') $r
    return $r
}
function Recover([string]$Root){$r=Repair-RPGTrainingDevelopmentRun $Root;$proof.deletedBytes+=[long]$r.deletedBytes;return $r}
try{
    foreach($relative in @('Scripts/Start-TrainingDevelopment.ps1','Scripts/Tests/TrainingDevelopmentRecovery.ps1',$PSCommandPath)){
        $path=if([IO.Path]::IsPathRooted($relative)){$relative}else{Join-Path $workspace $relative}
        $t=$null;$e=$null;$null=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$t,[ref]$e);Need (!$e.Count) ('Parser failed: '+$relative)
    }
    $buildRoot=New-FixtureRun;$source=Join-Path $buildRoot 'Temp/FixtureSource';$fixtureBinary=Join-Path $buildRoot 'Temp/FixtureBuild'
    $null=New-Item -ItemType Directory -Path $source -Force
    [IO.File]::WriteAllText((Join-Path $source 'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net9.0</TargetFramework><OutputType>Exe</OutputType><AssemblyName>ProjectRpg.Backend</AssemblyName></PropertyGroup></Project>')
    [IO.File]::WriteAllText((Join-Path $source 'Program.cs'),'System.Threading.Thread.Sleep(180000);')
    $dotnet=(Get-Command dotnet).Source
    & $dotnet build (Join-Path $source 'Fixture.csproj') -c Release --artifacts-path (Join-Path $buildRoot 'Temp/Artifacts') --output $fixtureBinary *> (Join-Path $proofRoot 'FixtureBuild.log')
    Need ($LASTEXITCODE -eq 0) 'Standalone sleeper fixture build failed'
    $root=New-FixtureRun
    $dest=Join-Path $root 'Temp/Build';$null=New-Item -ItemType Directory -Path $dest -Force
    foreach($file in Get-ChildItem -LiteralPath $fixtureBinary -File){Copy-Item -LiteralPath $file.FullName -Destination $dest}
    $parentScript=Join-Path $buildRoot 'Temp/Parent.ps1'
    $parentSource=@'
param([string]$Workspace,[string]$RunRoot,[string]$Dotnet)
$ErrorActionPreference='Stop'
. (Join-Path $Workspace 'Scripts/Tests/TrainingDevelopmentRecovery.ps1')
$r=New-RPGTrainingLifecycle $RunRoot 'D:/UE_5.8'
$p=Start-Process $Dotnet -ArgumentList ('"'+(Join-Path $RunRoot 'Temp/Build/ProjectRpg.Backend.dll')+'"') -PassThru -WindowStyle Hidden
$null=$p.Handle
Update-RPGTrainingLifecycle $RunRoot $r @(@{process=$p;path=$p.Path;ticks=$p.StartTime.ToUniversalTime().Ticks})
[IO.File]::WriteAllText((Join-Path $RunRoot 'ready'),'ready')
Start-Sleep -Seconds 150
'@
    [IO.File]::WriteAllText($parentScript,$parentSource,[Text.UTF8Encoding]::new($false))
    $ps=(Get-Process -Id $PID).Path
    $parent=Track (Start-Process $ps -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$parentScript+'"'),'-Workspace',('"'+$workspace+'"'),'-RunRoot',('"'+$root+'"'),'-Dotnet',('"'+$dotnet+'"')) -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $proofRoot 'FixtureParent.log') -RedirectStandardError (Join-Path $proofRoot 'FixtureParent.stderr.log'))
    $until=[DateTime]::UtcNow.AddSeconds(20)
    while(!(Test-Path (Join-Path $root 'ready'))){if($parent.HasExited -or [DateTime]::UtcNow -ge $until){throw 'Fixture parent failed to register its child'};Start-Sleep -Milliseconds 100}
    $journal=Get-Content (Join-Path $root 'Lifecycle.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $child=Track (Get-Process -Id $journal.children[0].pid)
    Need ((Recover $root).status -eq 'ActiveParent' -and !$child.HasExited -and (Test-Path (Join-Path $root 'Temp'))) 'A live launcher was not protected'
    $proof.groups+='LiveParentProtected'
    $deadParent=Get-RPGTrainingProcessIdentity $parent
    $parent.Kill();Need ($parent.WaitForExit(10000)) 'Owned parent did not stop'
    Need (!$child.HasExited) 'Sleeper did not survive parent termination'
    $sentinel=Join-Path $buildRoot 'Temp/OriginalSentinel';$null=New-Item -ItemType Directory -Path $sentinel -Force
    [IO.File]::WriteAllText((Join-Path $sentinel 'keep.txt'),'preserve')
    $null=New-Item -ItemType Junction -Path (Join-Path $root 'Temp/Content') -Target $sentinel
    $userParent=Join-Path $workspace ('Saved/Tests/TP/'+(Split-Path -Leaf $root).Substring(0,12))
    $null=New-Item -ItemType Directory -Path (Join-Path $userParent 'U') -Force
    [IO.File]::WriteAllText((Join-Path $userParent 'U/cache.bin'),'temporary')
    $r=Recover $root
    Need ($r.status -eq 'Recovered' -and $r.stoppedProcesses -eq 1 -and $r.temporaryPathsAbsent) 'Orphan recovery failed'
    Need ($child.WaitForExit(10000) -and (Test-Path (Join-Path $sentinel 'keep.txt'))) 'Owned child or junction boundary was wrong'
    Need ((Recover $root).status -eq 'Completed') 'Repeat recovery was not idempotent'
    $proof.groups+='ActualParentTerminationAndChildRecovery','JunctionTargetAndUserDir','Idempotence'

    $root=New-FixtureRun;$child=Prepare-Child $root;$r=Orphan-Record $root $child
    $r.children[0].startTicks=[long]$r.children[0].startTicks+1
    Write-RPGTrainingLifecycle (Join-Path $root 'Lifecycle.json') $r
    Reject {Recover $root} 'child identity differs'
    Need (!$child.HasExited -and (Test-Path (Join-Path $root 'Temp'))) 'Uncertain process identity was not preserved'
    $child.Kill();$null=$child.WaitForExit(10000);$null=Recover $root
    $proof.groups+='ChangedStartTimeRejectsBeforeStop'

    $other=New-FixtureRun;$child=Prepare-Child $other
    $root=New-FixtureRun;$r=Orphan-Record $root $child
    Reject {Recover $root} 'exact runtime argument'
    Need (!$child.HasExited) 'A child belonging to a different runtime was stopped'
    $child.Kill();$null=$child.WaitForExit(10000);$null=Recover $root
    $proof.groups+='ForeignRuntimeArgumentProtected'

    $root=New-FixtureRun;$child=Prepare-Child $root;$null=Orphan-Record $root $null
    Reject {Recover $root} 'unregistered runtime process remains'
    Need (!$child.HasExited -and (Test-Path (Join-Path $root 'Temp'))) 'Unregistered process was guessed into ownership'
    $child.Kill();$null=$child.WaitForExit(10000);$null=Recover $root
    $proof.groups+='LaunchJournalGapRetained'

    $root=New-FixtureRun;$r=Orphan-Record $root $null
    $r.runId='wrong';Write-RPGTrainingLifecycle (Join-Path $root 'Lifecycle.json') $r
    Reject {Recover $root} 'mismatched recovery ownership'
    Need (Test-Path (Join-Path $root 'Temp')) 'Malformed journal deleted files'
    $r.runId=Split-Path -Leaf $root;Write-RPGTrainingLifecycle (Join-Path $root 'Lifecycle.json') $r
    $null=Recover $root
    Reject {Repair-RPGTrainingDevelopmentRun (Join-Path $workspace 'Config')} 'outside the managed GUID'
    $proof.groups+='JournalAndRootBoundary'

    $root=New-FixtureRun;$null=Orphan-Record $root $null
    [IO.Directory]::Delete((Join-Path $root 'Temp'),$false)
    $null=New-Item -ItemType Junction -Path (Join-Path $root 'Temp') -Target $sentinel
    Reject {Recover $root} 'traverses a reparse point'
    Need (Test-Path (Join-Path $sentinel 'keep.txt')) 'A reparse runtime reached its target'
    $c=Remove-RPGTrainingGuideTemporaryTree -Path (Join-Path $root 'Temp') -OwnedParent $root;$proof.deletedBytes+=[long]$c.deletedBytes
    $null=Recover $root
    $proof.groups+='ReparseRuntimeRejected'

    $root=New-FixtureRun;$null=Orphan-Record $root $null
    & {
        # Exercise the real reservation parser, including Windows path casing,
        # with a fixture read instead of editing the shared coordination file.
        foreach($fixtureReservationPath in @($root.Substring($workspace.Length+1).Replace('\','/').ToUpperInvariant(),'Scripts/Start-TrainingDevelopment.ps1; Saved/TrainingDevelopment/<other-guid>/Temp')){
            function Get-Content {
                param([string]$LiteralPath,[switch]$Raw,[string]$Encoding)
                if($LiteralPath.EndsWith('Online_RPG_Development_Roadmap.md')){
                    $state=[string][char]0xC9C4+[char]0xD589+' '+[char]0xC911
                    return ('| Other owner | Fixed input | '+$fixtureReservationPath+' | Reader | '+$state+' |')
                }
                Microsoft.PowerShell.Management\Get-Content @PSBoundParameters
            }
            Reject {Recover $root} 'Another active reservation'
        }
    }
    Need (Test-Path (Join-Path $root 'Temp')) 'Active reservation was ignored'
    $null=Recover $root
    $proof.groups+='ReservationProtected'

    $root=New-FixtureRun
    Need ((Recover $root).status -eq 'Unmanaged' -and (Test-Path (Join-Path $root 'Temp'))) 'A legacy unjournaled runtime was adopted'
    $proof.groups+='LegacyWithoutJournalUntouched'
    $active=New-FixtureRun;$null=New-RPGTrainingLifecycle $active 'D:/UE_5.8'
    $root=New-FixtureRun;$null=Orphan-Record $root $null
    [IO.File]::WriteAllText((Join-Path $root 'Temp/cache.bin'),'recovery-only')
    $reply=(& (Join-Path $PSScriptRoot '../Start-TrainingDevelopment.ps1') -RecoverOnly -EngineRoot 'Z:/missing-engine')|ConvertFrom-Json
    $done=@($reply|Where-Object runId -eq (Split-Path -Leaf $root))
    Need ($done.Count -eq 1 -and $done[0].status -eq 'Recovered' -and !(Test-Path (Join-Path $root 'Temp'))) 'Recovery-only CLI did not finish before engine prerequisites'
    Need (Test-Path (Join-Path $active 'Temp')) 'Recovery-only CLI disturbed a live parent'
    $proof.deletedBytes+=[long]$done[0].deletedBytes
    Reject {& (Join-Path $PSScriptRoot '../Start-TrainingDevelopment.ps1') -RecoverOnly -SmokeTest} 'Recovery-only cannot launch'
    $proof.groups+='RecoveryOnlyCLIAndModePreflight'
    foreach($path in $runs){$j=Join-Path $path 'Lifecycle.json';if(Test-Path $j){$text=Get-Content $j -Raw -Encoding UTF8;Need ($text -notmatch '"(?:token|ticket|authorization|password|commandline)"\s*:') 'Journal contains credential/command-line data'}}
    $proof.groups+='CredentialFreeJournal'
    $proof.passed=$true
}catch{$proof.failure=$_.Exception.Message}
finally{
    # Also retain exact child identities from this test's journal if the fixture
    # parent failed after spawning but before writing its ready marker.
    foreach($root in $runs){foreach($suffix in @('Lifecycle.json','Lifecycle.json.next')){
        $j=Join-Path $root $suffix
        if(Test-Path $j){try{$r=Get-Content $j -Raw -Encoding UTF8|ConvertFrom-Json;foreach($child in $r.children){$p=Get-Process -Id $child.pid -ErrorAction SilentlyContinue;if($p){if(Test-RPGTrainingIdentity $child $p){$null=Track $p}else{$p.Dispose()}}}}catch{$proof.cleanupExceptions+=@($_.Exception.Message)}}
    }}
    foreach($entry in $owned){try{$p=$entry.process;if(!$p.HasExited){Need (Test-RPGTrainingIdentity $entry.identity $p) 'Owned fixture identity changed';$p.Kill()};Need ($p.WaitForExit(10000)) 'Owned fixture still running';$p.Dispose()}catch{$proof.cleanupExceptions+=@($_.Exception.Message)}}
    foreach($root in $runs){try{
        $id=Split-Path -Leaf $root;$parent=Join-Path $workspace ('Saved/Tests/TP/'+$id.Substring(0,12));$users=Join-Path $parent 'U'
        $c=Remove-RPGTrainingGuideTemporaryTree -Path $users -OwnedParent $parent;$proof.deletedBytes+=[long]$c.deletedBytes
        if(Test-Path $parent){[IO.Directory]::Delete($parent,$false)}
        $c=Remove-RPGTrainingGuideTemporaryTree -Path (Join-Path $root 'Temp') -OwnedParent $root;$proof.deletedBytes+=[long]$c.deletedBytes
        $j=Join-Path $root 'Lifecycle.json'
        if(Test-Path $j){$r=Get-Content $j -Raw -Encoding UTF8|ConvertFrom-Json;$r.completed=$true;Write-RPGTrainingLifecycle $j $r}
    }catch{$proof.cleanupExceptions+=@($_.Exception.Message)}}
    $proof.passed=$proof.passed -and !$proof.cleanupExceptions.Count
    $proof.runIds=@($runs|ForEach-Object {Split-Path -Leaf $_})
    $proof.temporaryPathsAbsent=!@($runs|Where-Object {Test-Path (Join-Path $_ 'Temp')}).Count
    $proof|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $proofRoot ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'.json')) -Encoding UTF8
    $proof|ConvertTo-Json -Depth 7
}
if(!$proof.passed){throw $proof.failure}
