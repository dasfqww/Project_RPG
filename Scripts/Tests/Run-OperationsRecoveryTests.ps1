[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$opsWorkspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$opsRoot = Join-Path $opsWorkspace 'Saved/BuildChecks/OperationsRecovery-20261108'
$opsTemp = Join-Path $opsRoot 'Temp'
$opsRunId = [Guid]::NewGuid().ToString('N')
$opsRunRoot = Join-Path $opsTemp $opsRunId
$opsInputs = Join-Path $opsRunRoot 'Inputs/Backend'
$opsArtifacts = Join-Path $opsRunRoot 'Artifacts'
$opsContainerName = 'rpg-ops-recovery-' + $opsRunId
$opsContainerId = $null
$opsOldRoot = $env:PROJECT_RPG_OPS_TEST_ROOT
$opsOldPg = $env:PROJECT_RPG_OPS_TEST_POSTGRES
$opsOldRun = $env:PROJECT_RPG_OPS_TEST_RUN

$opsResult = [ordered]@{ Passed=$false; RunId=$opsRunId; NativeBackup=$true; ContainerDeletedBytes=0L;  BaseCommit=$null; Inputs=@(); Commands=@(); DeletedBytes=0L; CleanupComplete=$false; ContainerRemoved=$false; Error=$null }

function Assert-OpsReservation {
    $row = Get-Content -LiteralPath (Join-Path $opsWorkspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object { $_ -match '^\|.*operations-recovery-20261108' }
    if (@($row).Count -ne 1) { throw 'Operations task reservation is missing.' }
}
function Get-OpsContainer {
    $found = @(& docker container ls --all --filter ('name=^/' + $opsContainerName + '$') --format '{{.ID}}')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect owned container state.' }
    if ($found.Count -eq 0) { return $null }
    if ($found.Count -ne 1) { throw 'Ambiguous owned container.' }
    $info = (& docker inspect $found[0] | ConvertFrom-Json)[0]
    if ($LASTEXITCODE -ne 0 -or $info.Name -cne ('/' + $opsContainerName) -or $info.Config.Labels.'project-rpg.run' -cne $opsRunId -or $info.Config.Labels.'project-rpg.task' -cne 'operations-recovery-20261108') { throw 'Container ownership mismatch.' }
    return $info
}
function Remove-OpsTemp {
    Assert-OpsReservation
    $absolute = [IO.Path]::GetFullPath($opsRunRoot)
    $expected = [IO.Path]::GetFullPath((Join-Path $opsTemp $opsRunId))
    if ($absolute -cne $expected -or -not $absolute.StartsWith($opsWorkspace + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup escaped owned Temp.' }
    if (-not (Test-Path -LiteralPath $absolute)) { $opsResult.CleanupComplete=$true; return }
    $queue = [Collections.Generic.Stack[string]]::new(); $queue.Push($absolute)
    [long]$bytes=0
    while($queue.Count -gt 0) {
        $directory=Get-Item -LiteralPath $queue.Pop() -Force
        if($directory.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Cleanup refuses linked directories.'}
        foreach($item in Get-ChildItem -LiteralPath $directory.FullName -Force){
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Cleanup refuses linked entries.'}
            if($item.PSIsContainer){$queue.Push($item.FullName)}else{$bytes += $item.Length}
        }
    }
    Remove-Item -LiteralPath $absolute -Recurse -Force
    $opsResult.DeletedBytes=$bytes; $opsResult.CleanupComplete= -not (Test-Path -LiteralPath $absolute)
    if (@(Get-ChildItem -LiteralPath $opsTemp -Force).Count -eq 0) { [IO.Directory]::Delete($opsTemp,$false) }
}

New-Item -ItemType Directory -Path $opsRunRoot -Force | Out-Null
try {
    Assert-OpsReservation
    # Freeze published dependencies; overlay only this task's source. Other agents
    # can continue editing narrative/UE code without entering this test input.
    $opsResult.BaseCommit=([string](& git -C $opsWorkspace rev-parse HEAD)).Trim()
    $archive=Join-Path $opsRunRoot 'published-backend.zip'
    & git -C $opsWorkspace archive --format=zip ('--output='+$archive) $opsResult.BaseCommit Backend/Database
    if($LASTEXITCODE -ne 0){throw 'Published dependency snapshot failed.'}
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $opsRunRoot 'Inputs')
    $scriptInputs=@('Scripts/Operations/backup_status.py','Scripts/Operations/restore_rehearsal.py','Scripts/Tests/test_restore_rehearsal.py','Scripts/Tests/test_backup_status.py','Scripts/Tests/operations_recovery_fixture.py')
    foreach($relative in $scriptInputs){
        $source=Join-Path $opsWorkspace $relative
        $target=Join-Path (Join-Path $opsRunRoot 'Inputs') $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
        $opsResult.Inputs += @{Path=$relative;SHA256=(Get-FileHash -LiteralPath $source).Hash}
        if((Get-FileHash -LiteralPath $target).Hash -cne (Get-FileHash -LiteralPath $source).Hash){throw 'Script snapshot mismatch.'}
    }
    if($true){
        & docker image inspect postgres:17 --format '{{.Id}}' *> $null
        if($LASTEXITCODE -ne 0){throw 'Cached postgres:17 image is required.'}
        $opsPassword=[Guid]::NewGuid().ToString('N')+[Guid]::NewGuid().ToString('N')
        $opsContainerId = & docker run --pull=never --detach --name $opsContainerName --label ('project-rpg.run='+$opsRunId) --label 'project-rpg.task=operations-recovery-20261108' --memory 1g --cpus 1 --pids-limit 128 --tmpfs '/var/lib/postgresql/data:rw,size=268435456' --tmpfs '/backup-repo:rw,size=134217728' --tmpfs '/restore-work:rw,size=268435456' --env 'POSTGRES_DB=ops_test' --env 'POSTGRES_USER=postgres' --env ('POSTGRES_PASSWORD='+$opsPassword) postgres:17 -c shared_buffers=16MB -c max_connections=32 -c archive_mode=on -c archive_timeout=300 -c "archive_command=pgbackrest --config=/verify/pgbackrest.conf --stanza=fixture archive-push %p"
        if($LASTEXITCODE -ne 0){throw 'Owned PostgreSQL could not start.'}
        $info=Get-OpsContainer
        if($info.Id -cne $opsContainerId -or @($info.Mounts | Where-Object Type -ne 'tmpfs').Count -ne 0){throw 'Unexpected PostgreSQL identity/mounts.'}
        $ready=$false
        for($attempt=0;$attempt -lt 40;$attempt++){
            & docker exec $opsContainerId pg_isready -U ops_test -d ops_test *> $null
            if($LASTEXITCODE -eq 0){$ready=$true;break}
            Start-Sleep -Seconds 1
        }
        if(-not $ready){throw 'Owned PostgreSQL readiness timeout.'}
        $ErrorActionPreference='Continue'
        & docker exec --env DEBIAN_FRONTEND=noninteractive $opsContainerId bash -c 'apt-get update -qq && apt-get install -y -qq --no-install-recommends pgbackrest python3 > /dev/null' *> (Join-Path $opsRoot ('NativeSetup-'+$opsRunId+'.log'))
        $setupExit=$LASTEXITCODE; $ErrorActionPreference='Stop'
        if($setupExit -ne 0){throw 'Owned native package installation failed.'}
        # Docker cp on the current Desktop daemon writes behind tmpfs mounts.
        # Keep only DB/repository in tmpfs; copied verification inputs use the owned disposable layer.
        & docker exec $opsContainerId mkdir -p /verify
        if($LASTEXITCODE -ne 0){throw 'Owned native input directory failed.'}
        foreach($relative in $scriptInputs){
            $scriptInput=(Get-Item -LiteralPath (Join-Path (Join-Path $opsRunRoot 'Inputs') $relative)).FullName
            $nativeScript='/verify/'+[IO.Path]::GetFileName($scriptInput)
            & docker cp $scriptInput ($opsContainerId+':'+$nativeScript) *> $null
            if($LASTEXITCODE -ne 0){throw 'Native script copy failed.'}
            $nativeHashLine=[string](& docker exec $opsContainerId sha256sum $nativeScript)
            if($LASTEXITCODE -ne 0 -or ($nativeHashLine -split '\s+')[0] -ine (Get-FileHash -LiteralPath $scriptInput).Hash){throw 'Native script content mismatch.'}
        }
        & docker cp (Join-Path $opsInputs 'Database') ($opsContainerId+':/verify/Database') *> $null
        if($LASTEXITCODE -ne 0){throw 'Published database SQL copy failed.'}
        & docker exec $opsContainerId chown -R postgres:postgres /verify /backup-repo /restore-work
        if($LASTEXITCODE -ne 0){throw 'Owned native fixture permissions failed.'}
        $ErrorActionPreference='Continue'
        & docker exec --user postgres $opsContainerId timeout 600 python3 /verify/operations_recovery_fixture.py *> (Join-Path $opsRoot ('Native-'+$opsRunId+'.log'))
        $nativeExit=$LASTEXITCODE; $ErrorActionPreference='Stop'

        $nativeDirectory=Join-Path $opsRunRoot 'Runtime/Native'
        New-Item -ItemType Directory -Path $nativeDirectory -Force | Out-Null
        & docker cp ($opsContainerId+':/verify/results/.') $nativeDirectory *> $null
        if($LASTEXITCODE -ne 0){throw 'Native result transfer failed.'}
        Copy-Item -LiteralPath (Join-Path $nativeDirectory 'result.json') -Destination (Join-Path $opsRoot ('NativeResult-'+$opsRunId+'.json'))
        $privateProof=Join-Path $opsRoot ('Proof-'+$opsRunId)
        New-Item -ItemType Directory -Path $privateProof -Force | Out-Null
        $proofFiles=@(Get-ChildItem -LiteralPath $nativeDirectory -File)
        if($proofFiles.Count -gt 32){throw 'Native proof exceeded file count bound.'}
        foreach($proofFile in $proofFiles){
            if($proofFile.Length -gt 65536 -or $proofFile.Extension -notin '.json','.log'){throw 'Native proof exceeded private contract.'}
            Copy-Item -LiteralPath $proofFile.FullName -Destination (Join-Path $privateProof $proofFile.Name)
        }
        if($nativeExit -ne 0){throw 'Native recovery fixture failed; see owned result/diagnostics.'}
    }
    foreach($input in $opsResult.Inputs){
        if((Get-FileHash -LiteralPath (Join-Path $opsWorkspace $input.Path)).Hash -cne $input.SHA256){throw 'Validated source changed.'}
        if((Get-FileHash -LiteralPath (Join-Path (Join-Path $opsRunRoot 'Inputs') $input.Path)).Hash -cne $input.SHA256){throw 'Closed source or package lock changed during build.'}
    }
    $nativeProof=Get-Content -LiteralPath (Join-Path $nativeDirectory 'result.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if(-not $nativeProof.passed){throw 'Recovery result did not pass.'}
    $opsResult.Commands += 'Owned Linux Python unit tests and actual encrypted PostgreSQL17 full/incr/PITR recovery; negative guards and cleanup'
    $opsResult.Passed=$true
} catch { $opsResult.Error=$_.Exception.Message; Write-Output $opsResult.Error }
finally {

    $env:PROJECT_RPG_OPS_TEST_ROOT=$opsOldRoot; $env:PROJECT_RPG_OPS_TEST_POSTGRES=$opsOldPg; $env:PROJECT_RPG_OPS_TEST_RUN=$opsOldRun
    try {
        if($opsContainerId){$owned=Get-OpsContainer;if($owned){$sizeInfo=(& docker inspect --size $owned.Id | ConvertFrom-Json)[0]; $opsResult.ContainerDeletedBytes=[long]$sizeInfo.SizeRw; & docker rm --force $owned.Id *> $null;if($LASTEXITCODE -ne 0){throw 'Owned container removal failed.'}};$opsResult.ContainerRemoved=$null -eq (Get-OpsContainer)}
        Remove-OpsTemp
    } catch { $opsResult.Error=([string]$opsResult.Error+'; cleanup: '+$_.Exception.Message);$opsResult.CleanupComplete=$false }
    $opsResult | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $opsRoot ('Result-'+$opsRunId+'.json')) -Encoding UTF8
    Write-Output ('Operations run '+$opsRunId+' passed='+$opsResult.Passed+' cleanup='+$opsResult.CleanupComplete+' deletedBytes='+$opsResult.DeletedBytes)
}
if(-not $opsResult.Passed -or -not $opsResult.CleanupComplete){exit 1}
