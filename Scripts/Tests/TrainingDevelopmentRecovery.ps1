# Durable ownership only. No credentials, command lines or gameplay state are saved.
. (Join-Path $PSScriptRoot 'TrainingGuideArtifacts.ps1')

function Write-RPGTrainingLifecycle([string]$Path,$Record){
    $next=$Path+'.next'
    Assert-RPGTrainingOrdinaryPath $Path
    Assert-RPGTrainingOrdinaryPath $next
    [IO.File]::WriteAllText($next,($Record|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    if(Test-Path -LiteralPath $Path){[IO.File]::Replace($next,$Path,[NullString]::Value)}else{[IO.File]::Move($next,$Path)}
}
function Get-RPGTrainingProcessIdentity($Process){
    @{pid=$Process.Id;path=$Process.Path;startTicks=$Process.StartTime.ToUniversalTime().Ticks}
}
function New-RPGTrainingLifecycle([string]$RunRoot,[string]$EngineRoot){
    $record=@{version=1;manager='ProjectRPG.TrainingDevelopment';runId=(Split-Path -Leaf $RunRoot);workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'));engineRoot=[IO.Path]::GetFullPath($EngineRoot);parent=(Get-RPGTrainingProcessIdentity (Get-Process -Id $PID));children=@();completed=$false}
    Write-RPGTrainingLifecycle (Join-Path $RunRoot 'Lifecycle.json') $record
    return $record
}
function Update-RPGTrainingLifecycle([string]$RunRoot,$Record,$Processes){
    $Record.children=@($Processes|ForEach-Object {@{pid=$_.process.Id;path=$_.path;startTicks=$_.ticks}})
    Write-RPGTrainingLifecycle (Join-Path $RunRoot 'Lifecycle.json') $Record
}
function Assert-RPGTrainingRecoveryReservation([string]$RunRoot,[string]$UserParent){
    $workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    foreach($line in Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8){
        if($line -notmatch '^\|' -or $line -match 'training-interruption-recovery-20261031'){continue}
        if(($line -split '\|')[-2] -notmatch '\uC9C4\uD589 \uC911|\uC608\uC57D \uC911|\uAC80\uC99D \uC911'){continue}
        $normalized=$line.Replace('\','/')
        # A generic reservation of this launcher/runtime family is also a
        # conflict. Missing a concrete GUID is not evidence of abandonment.
        if($normalized.IndexOf('Scripts/Start-TrainingDevelopment.ps1',[StringComparison]::OrdinalIgnoreCase) -ge 0 -or $normalized.IndexOf('Saved/TrainingDevelopment/',[StringComparison]::OrdinalIgnoreCase) -ge 0){throw 'Another active reservation references the interrupted runtime family'}
        foreach($path in @($RunRoot,$UserParent)){
            $relative=$path.Substring($workspace.Length+1).Replace('\','/')
            if($normalized.IndexOf($relative,[StringComparison]::OrdinalIgnoreCase) -ge 0){throw 'Another active reservation references the interrupted runtime'}
        }
    }
}
function Test-RPGTrainingIdentity($Identity,$Process){
    $Process.Refresh()
    return !$Process.HasExited -and $Process.Path -ieq $Identity.path -and $Process.StartTime.ToUniversalTime().Ticks -eq [long]$Identity.startTicks
}
function Assert-RPGTrainingOrdinaryPath([string]$Path){
    $cursor=[IO.Path]::GetFullPath($Path)
    while($cursor){
        if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Recovery path traverses a reparse point'}
        $cursor=Split-Path -Parent $cursor
    }
}
function Repair-RPGTrainingDevelopmentRun([string]$RunRoot){
    $workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    $run=[IO.Path]::GetFullPath($RunRoot).TrimEnd('\','/')
    $id=Split-Path -Leaf $run
    if($id -cnotmatch '^[a-f0-9]{32}$' -or $run -ine (Join-Path $workspace ('Saved/TrainingDevelopment/'+$id))){throw 'Recovery run is outside the managed GUID directory'}
    Assert-RPGTrainingOrdinaryPath $run
    $journal=Join-Path $run 'Lifecycle.json'
    if(!(Test-Path -LiteralPath $journal)){return @{runId=$id;status='Unmanaged';deletedBytes=0L}}
    Assert-RPGTrainingOrdinaryPath $journal
    $record=Get-Content -LiteralPath $journal -Raw -Encoding UTF8|ConvertFrom-Json
    if($record.version -ne 1 -or $record.manager -cne 'ProjectRPG.TrainingDevelopment' -or $record.runId -cne $id -or $record.workspace -ine $workspace){throw 'Unsupported or mismatched recovery ownership journal'}
    if($record.completed){return @{runId=$id;status='Completed';deletedBytes=0L}}
    $parent=Get-Process -Id ([int]$record.parent.pid) -ErrorAction SilentlyContinue
    if($parent){try{if(Test-RPGTrainingIdentity $record.parent $parent){return @{runId=$id;status='ActiveParent';deletedBytes=0L}}}finally{$parent.Dispose()}}
    $userParent=Join-Path $workspace ('Saved/Tests/TP/'+$id.Substring(0,12))
    $users=Join-Path $userParent 'U';$temp=Join-Path $run 'Temp'
    Assert-RPGTrainingOrdinaryPath $temp
    Assert-RPGTrainingOrdinaryPath $users
    Assert-RPGTrainingRecoveryReservation $run $userParent
    $project=Join-Path $temp 'RuntimeProject/Project_RPG.uproject'
    $backend=Join-Path $temp 'Build/ProjectRpg.Backend.dll'
    $engine=[IO.Path]::GetFullPath($record.engineRoot)
    $editors=@((Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'),(Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe'))
    $children=@($record.children)
    # The allocator persists exact process identity before its readiness wait.
    # Recover that one server even if the launcher was killed before registration.
    $allocator=Join-Path $temp 'Allocator'
    if(Test-Path -LiteralPath $allocator){
        Assert-RPGTrainingOrdinaryPath $allocator
        foreach($stateFile in Get-ChildItem -LiteralPath $allocator -Filter '*.allocator.json' -File){
            Assert-RPGTrainingOrdinaryPath $stateFile.FullName
            $state=Get-Content -LiteralPath $stateFile.FullName -Raw -Encoding UTF8|ConvertFrom-Json
            if($state.editorProject -ine $project -or $state.runtimeUserDirectory -ine (Join-Path $users 's')){throw 'Allocator ownership differs from the interrupted runtime'}
            if(!@($children|Where-Object {[int]$_.pid -eq [int]$state.processId}).Count){$children+=@{pid=$state.processId;startTicks=$state.processStartTimeUtcTicks;path=$editors[0]}}
        }
    }
    $owned=[Collections.Generic.List[object]]::new()
    $stopped=0
    try{
        # Validate every live child before stopping any. PID reuse or uncertain
        # identity preserves the runtime for its owner; it never kills a bystander.
        foreach($child in $children){
            $p=Get-Process -Id ([int]$child.pid) -ErrorAction SilentlyContinue
            if(!$p){continue}
            $added=$false
            try{
                if(!(Test-RPGTrainingIdentity $child $p)){throw 'Recovery child identity differs; no processes were stopped'}
                $cim=Get-CimInstance Win32_Process -Filter ('ProcessId='+$p.Id)
                $marker=if($p.Path -in $editors){$project}elseif([IO.Path]::GetFileName($p.Path) -ieq 'dotnet.exe'){$backend}else{throw 'Unrecognized managed executable'}
                # Match an argument, not a substring such as RuntimeProjectOther.
                $escaped=[regex]::Escape($marker)
                if(!$cim -or $cim.CommandLine -notmatch ('(?i)(?:^|\s|\")'+$escaped+'(?:\"|\s|$)')){throw 'Recovery process does not reference its exact runtime argument'}
                $null=$p.Handle;$owned.Add(@{process=$p;identity=$child});$added=$true
            }finally{if(!$added){$p.Dispose()}}
        }
        Assert-RPGTrainingRecoveryReservation $run $userParent
        foreach($entry in $owned){if(!(Test-RPGTrainingIdentity $entry.identity $entry.process)){throw 'Recovery identity changed before stop'}}
        foreach($entry in $owned){$entry.process.Kill();if(!$entry.process.WaitForExit(10000)){throw 'Owned recovery process did not release files'};$stopped++}
    }finally{foreach($entry in $owned){$entry.process.Dispose()}}
    # An unregistered process in the launch/journal gap must block deletion, not
    # be guessed into ownership from a command line alone.
    foreach($candidate in Get-CimInstance Win32_Process){
        if($candidate.CommandLine -and ($candidate.CommandLine.IndexOf($project,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or $candidate.CommandLine.IndexOf($backend,[StringComparison]::OrdinalIgnoreCase) -ge 0)){throw 'An unregistered runtime process remains; recovery files retained'}
    }
    Assert-RPGTrainingRecoveryReservation $run $userParent
    $userCleanup=Remove-RPGTrainingGuideTemporaryTree -Path $users -OwnedParent $userParent
    if(Test-Path -LiteralPath $userParent){[IO.Directory]::Delete($userParent,$false)}
    $cleanup=Remove-RPGTrainingGuideTemporaryTree -Path $temp -OwnedParent $run
    $record.completed=$true
    Write-RPGTrainingLifecycle $journal $record
    $result=@{runId=$id;status='Recovered';stoppedProcesses=$stopped;deletedBytes=([long]$cleanup.deletedBytes+[long]$userCleanup.deletedBytes);temporaryPathsAbsent=($cleanup.absent -and $userCleanup.absent)}
    $result|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'Recovery.json') -Encoding UTF8
    return $result
}
function Repair-RPGTrainingDevelopmentRuns {
    $base=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))) 'Saved/TrainingDevelopment'
    if(!(Test-Path -LiteralPath $base)){return}
    foreach($directory in Get-ChildItem -LiteralPath $base -Directory){
        if($directory.Name -cmatch '^[a-f0-9]{32}$' -and (Test-Path -LiteralPath (Join-Path $directory.FullName 'Lifecycle.json'))){Repair-RPGTrainingDevelopmentRun $directory.FullName}
    }
}
