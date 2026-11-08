[CmdletBinding()]
param([switch]$Postgres, [switch]$FullSuite)

$ErrorActionPreference = 'Stop'
$opsWorkspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$opsRoot = Join-Path $opsWorkspace 'Saved/BuildChecks/OperationsPickupTracing-20261108'
$opsTemp = Join-Path $opsRoot 'Temp'
$opsRunId = [Guid]::NewGuid().ToString('N')
$opsRunRoot = Join-Path $opsTemp $opsRunId
$opsInputs = Join-Path $opsRunRoot 'Inputs/Backend'
$opsArtifacts = Join-Path $opsRunRoot 'Artifacts'
$opsContainerName = 'rpg-ops-pickup-tracing-' + $opsRunId
$opsContainerId = $null
$opsOldRoot = $env:PROJECT_RPG_OPS_TEST_ROOT
$opsOldPg = $env:PROJECT_RPG_OPS_TEST_POSTGRES
$opsOldRun = $env:PROJECT_RPG_OPS_TEST_RUN
$opsResult = [ordered]@{ Passed=$false; RunId=$opsRunId; Postgres=[bool]$Postgres; FullSuite=[bool]$FullSuite; BaseCommit=$null; Inputs=@(); Commands=@(); DeletedBytes=0L; CleanupComplete=$false; ContainerRemoved=$false; Error=$null }

function Assert-OpsReservation {
    $row = Get-Content -LiteralPath (Join-Path $opsWorkspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object { $_ -match '^\|.*operations-pickup-tracing-20261108' }
    if (@($row).Count -ne 1) { throw 'Operations task reservation is missing.' }
}
function Get-OpsContainer {
    $found = @(& docker container ls --all --filter ('name=^/' + $opsContainerName + '$') --format '{{.ID}}')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect owned container state.' }
    if ($found.Count -eq 0) { return $null }
    if ($found.Count -ne 1) { throw 'Ambiguous owned container.' }
    $info = (& docker inspect $found[0] | ConvertFrom-Json)[0]
    if ($LASTEXITCODE -ne 0 -or $info.Name -cne ('/' + $opsContainerName) -or $info.Config.Labels.'project-rpg.run' -cne $opsRunId -or $info.Config.Labels.'project-rpg.task' -cne 'operations-pickup-tracing-20261108') { throw 'Container ownership mismatch.' }
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
    & git -C $opsWorkspace archive --format=zip ('--output='+$archive) $opsResult.BaseCommit Backend
    if($LASTEXITCODE -ne 0){throw 'Published dependency snapshot failed.'}
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $opsRunRoot 'Inputs')
    $owned=@('ProjectRpg.Backend/Application/DungeonPickupCommitService.cs','ProjectRpg.Backend/Application/IGameTransactionObserver.cs','ProjectRpg.Backend/Operations/Infrastructure/Diagnostics/OperationsEventRecorder.cs','ProjectRpg.Operations.Tests/Program.cs','ProjectRpg.Operations.Tests/PickupDiagnosticsTests.cs')
    foreach($relative in $owned){
        $source=Join-Path $opsWorkspace ('Backend/'+$relative)
        $sourceItem=Get-Item -LiteralPath $source
        $files=if($sourceItem.PSIsContainer){@(Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object { $_.FullName -notmatch '[\\/](bin|obj)[\\/]' -and $_.Extension -in '.cs','.csproj','.cshtml','.json','.sql' })}else{@($sourceItem)}
        foreach($file in $files){
            if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked source refused.'}
            $hash=(Get-FileHash -LiteralPath $file.FullName).Hash
            $target=Join-Path (Join-Path $opsRunRoot 'Inputs') $file.FullName.Substring($opsWorkspace.Length+1)
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target
            if((Get-FileHash -LiteralPath $file.FullName).Hash -cne $hash -or (Get-FileHash -LiteralPath $target).Hash -cne $hash){throw 'Input changed during snapshot.'}
            $opsResult.Inputs += @{Path=$file.FullName.Substring($opsWorkspace.Length+1);SHA256=$hash}
        }
    }
    $project=Join-Path $opsInputs 'ProjectRpg.Operations.Tests/ProjectRpg.Operations.Tests.csproj'
    $opsResult.Commands += 'dotnet build [owned snapshot Operations.Tests] --configuration Release --artifacts-path [owned Artifacts]'
    & dotnet build $project --configuration Release --artifacts-path $opsArtifacts --nologo *> (Join-Path $opsRoot ('Build-'+$opsRunId+'.log'))
    if($LASTEXITCODE -ne 0){throw 'Operations build failed; see owned build log.'}
    if($Postgres){
        & docker image inspect postgres:17-alpine --format '{{.Id}}' *> $null
        if($LASTEXITCODE -ne 0){throw 'Cached postgres:17-alpine image is required.'}
        $opsPassword=[Guid]::NewGuid().ToString('N')+[Guid]::NewGuid().ToString('N')
        $opsContainerId = & docker run --pull=never --detach --name $opsContainerName --label ('project-rpg.run='+$opsRunId) --label 'project-rpg.task=operations-pickup-tracing-20261108' --memory 512m --cpus 1 --pids-limit 128 --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=201326592' --env 'POSTGRES_DB=ops_test' --env 'POSTGRES_USER=ops_test' --env ('POSTGRES_PASSWORD='+$opsPassword) postgres:17-alpine -c shared_buffers=16MB -c max_connections=32
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
        $binding=[string](& docker port $opsContainerId 5432/tcp)
        if($LASTEXITCODE -ne 0 -or $binding.Trim() -notmatch '^127\.0\.0\.1:(\d+)$'){throw 'Unexpected PG binding.'}
        $env:PROJECT_RPG_OPS_TEST_POSTGRES='Host=127.0.0.1;Port='+$Matches[1]+';Database=ops_test;Username=ops_test;Password='+$opsPassword
        $env:PROJECT_RPG_OPS_TEST_RUN=$opsRunId
    } else { $env:PROJECT_RPG_OPS_TEST_POSTGRES=$null; $env:PROJECT_RPG_OPS_TEST_RUN=$null }
    $env:PROJECT_RPG_OPS_TEST_ROOT=Join-Path $opsRunRoot 'Runtime'
    $dll=Join-Path $opsArtifacts 'bin/ProjectRpg.Operations.Tests/release/ProjectRpg.Operations.Tests.dll'
    [string[]]$opsTestArgs = if($FullSuite){@()}else{@('--pickup-tracing')}
    $opsResult.Commands += ('dotnet [owned Operations.Tests.dll] '+($opsTestArgs -join ' ')+'; local HTTPS + mocked OAuth backchannel; optional owned tmpfs PostgreSQL')
    $ErrorActionPreference = 'Continue' # Windows PowerShell wraps native stderr in ErrorRecords.
    & dotnet $dll @opsTestArgs *> (Join-Path $opsRoot ('Tests-'+$opsRunId+'.log'))
    $ErrorActionPreference = 'Stop'
    if($LASTEXITCODE -ne 0){throw 'Operations contract/HTTP tests failed; see owned test log.'}
    foreach($opsEvidence in $opsResult.Inputs){
        if((Get-FileHash -LiteralPath (Join-Path $opsWorkspace $opsEvidence.Path)).Hash -cne $opsEvidence.SHA256){throw 'Validated source changed.'}
        if((Get-FileHash -LiteralPath (Join-Path (Join-Path $opsRunRoot 'Inputs') $opsEvidence.Path)).Hash -cne $opsEvidence.SHA256){throw 'Closed source or package lock changed during build.'}
    }
    $opsResult.Passed=$true
} catch { $opsResult.Error=$_.Exception.Message; Write-Output $opsResult.Error }
finally {
    $env:PROJECT_RPG_OPS_TEST_ROOT=$opsOldRoot; $env:PROJECT_RPG_OPS_TEST_POSTGRES=$opsOldPg; $env:PROJECT_RPG_OPS_TEST_RUN=$opsOldRun
    try {
        if($opsContainerId){$owned=Get-OpsContainer;if($owned){& docker rm --force $owned.Id *> $null;if($LASTEXITCODE -ne 0){throw 'Owned container removal failed.'}};$opsResult.ContainerRemoved=$null -eq (Get-OpsContainer)}
        Remove-OpsTemp
    } catch { $opsResult.Error=([string]$opsResult.Error+'; cleanup: '+$_.Exception.Message);$opsResult.CleanupComplete=$false }
    $opsResult | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $opsRoot ('Result-'+$opsRunId+'.json')) -Encoding UTF8
    Write-Output ('Operations run '+$opsRunId+' passed='+$opsResult.Passed+' cleanup='+$opsResult.CleanupComplete+' deletedBytes='+$opsResult.DeletedBytes)
}
if(-not $opsResult.Passed -or -not $opsResult.CleanupComplete){exit 1}
