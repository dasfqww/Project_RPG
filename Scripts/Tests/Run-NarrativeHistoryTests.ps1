[CmdletBinding()]
param([switch]$Postgres, [string]$BaseCommit='HEAD',
 [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$TaskId='narrative-history-20261110',
 [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$ProofName='NarrativeHistory-20261110')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$task=$TaskId
$runId=[Guid]::NewGuid().ToString('N')
$run=Join-Path $workspace ('Saved/BuildChecks/'+$ProofName+'/'+$runId)
$temp=Join-Path $run 'Temp';$inputs=Join-Path $temp 'Repo';$artifacts=Join-Path $temp 'artifacts'
$containerName='rpg-narrative-history-'+$runId;$containerId=$null
$result=[ordered]@{Passed=$false;BaseCommit=$null;Postgres=[bool]$Postgres;Inputs=@();InputsUnchanged=$false;Cleanup=$null;ContainerDeleted=$false;TempDeleted=$false;Failure=$null}
$result.RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash
$overlay=@(
 'Backend/ProjectRpg.Backend/Narrative/NarrativeTypes.cs',
 'Backend/ProjectRpg.Backend/Narrative/NarrativeCatalog.cs',
 'Backend/ProjectRpg.Backend/Data/INarrativeHistoryRepository.cs',
 'Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.NarrativeHistory.cs',
 'Backend/ProjectRpg.Backend/Data/PostgresGameRepository.NarrativeHistory.cs',
 'Backend/ProjectRpg.Backend/Narrative/NarrativeAccountHistory.cs',
 'Backend/ProjectRpg.Backend/Api/NarrativeApiEndpoints.cs',
 'Backend/ProjectRpg.Backend/Program.cs',
 'Backend/ProjectRpg.Backend.NarrativeHistoryTests/Program.cs',
 'Backend/ProjectRpg.Backend.NarrativeHistoryTests/ProjectRpg.Backend.NarrativeHistoryTests.csproj')
$null=New-Item -ItemType Directory -Path $inputs -Force
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Reservation {
 if(@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match ('^\|[^|]*'+$task+'(?: |\|)')}).Count -ne 1){throw 'Own reservation missing'}
}
function Invoke-Child([string]$Arguments,[string]$Log) {
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=(Get-Command dotnet -ErrorAction Stop).Source
 $start.Arguments=$Arguments;$start.WorkingDirectory=Join-Path $inputs 'Backend/ProjectRpg.Backend'
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle='Hidden'
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 foreach($key in @('POSTGRES_CONNECTION_STRING','PROJECT_RPG_BACKEND_ADMIN_TOKEN','STEAM_WEB_API_PUBLISHER_KEY','STEAM_APP_ID','PROJECT_RPG_NARRATIVE_TEST_POSTGRES','PROJECT_RPG_NARRATIVE_TEST_RUN')){$start.EnvironmentVariables.Remove($key)}
 $start.EnvironmentVariables['ASPNETCORE_ENVIRONMENT']='Testing';$start.EnvironmentVariables['DOTNET_ENVIRONMENT']='Testing'
 if($Postgres){$start.EnvironmentVariables['PROJECT_RPG_NARRATIVE_TEST_POSTGRES']=$postgresConnection;$start.EnvironmentVariables['PROJECT_RPG_NARRATIVE_TEST_RUN']=$runId}
 $child=[Diagnostics.Process]::new();$child.StartInfo=$start
 try {
  if(!$child.Start()){throw 'Own dotnet process failed to start'}
  $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
  if(!$child.WaitForExit(180000)){$child.Kill();$child.WaitForExit();throw ('Own child timed out: '+$Log)}
  [IO.File]::WriteAllText((Join-Path $run $Log),$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult())
  if($child.ExitCode -ne 0){throw ('Own child failed: '+$Log)}
  Write-Output ('PASS '+$Log)
 } finally {if($child.Id -and !$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
}
function Own-Container {
 $ids=@(& docker container ls -a --filter ('name=^/'+$containerName+'$') --format '{{.ID}}')
 if($LASTEXITCODE -ne 0){throw 'Cannot inspect own container'}
 if($ids.Count -eq 0){return $null}
 if($ids.Count -ne 1){throw 'Ambiguous own container'}
 $info=((& docker inspect $ids[0])|ConvertFrom-Json)[0]
 if($LASTEXITCODE -ne 0 -or $info.Name -cne ('/'+$containerName) -or $info.Config.Labels.'project-rpg.task' -cne $task -or
  $info.Config.Labels.'project-rpg.run' -cne $runId -or ($containerId -and $info.Id -cne $containerId)){throw 'Own container identity mismatch'}
 return $info
}
try {
 Reservation
 $revision=(& git -C $workspace rev-parse --verify ($BaseCommit+'^{commit}')).Trim()
 if($LASTEXITCODE -ne 0 -or $revision -notmatch '^[a-f0-9]{40}$'){throw 'Invalid immutable base'}
 $result.BaseCommit=$revision
 $archive=Join-Path $temp 'Backend.tar'
 & git -C $workspace archive --format=tar ('--output='+$archive) $revision Backend
 if($LASTEXITCODE -ne 0){throw 'Snapshot archive failed'}
 & tar -xf $archive -C $inputs
 if($LASTEXITCODE -ne 0){throw 'Snapshot extraction failed'}
 foreach($path in $overlay){
  $source=Join-Path $workspace $path;$destination=Join-Path $inputs $path
  if((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked overlay refused'}
  $null=New-Item -ItemType Directory -Path (Split-Path $destination) -Force
  Copy-Item -LiteralPath $source -Destination $destination
  $hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  if((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -cne $hash){throw 'Overlay changed during copy'}
  $result.Inputs+=@{Path=$path;SHA256=$hash}
 }
 if($Postgres){
  $password=[Guid]::NewGuid().ToString('N')
  $created=& docker run --detach --name $containerName --label ('project-rpg.task='+$task) --label ('project-rpg.run='+$runId) --memory 256m --cpus 1 --pids-limit 128 --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,nosuid,nodev,size=256m' --env 'POSTGRES_DB=narrative_test' --env 'POSTGRES_USER=narrative_test' --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine -c shared_buffers=16MB -c max_connections=32
  if($LASTEXITCODE -ne 0){throw 'Own PostgreSQL startup failed'}
  $containerId=([string]$created).Trim();$info=Own-Container
  if(@($info.Mounts|Where-Object Type -ne 'tmpfs').Count -ne 0){throw 'Unexpected persistent mount'}
  $ready=$false
  for($i=0;$i -lt 40;$i++){& docker exec $containerId pg_isready -U narrative_test -d narrative_test *> $null;if($LASTEXITCODE -eq 0){$ready=$true;break};Start-Sleep -Seconds 1}
  if(!$ready){throw 'Own PostgreSQL not ready'}
  $binding=[string](& docker port $containerId 5432/tcp)
  if($LASTEXITCODE -ne 0 -or $binding.Trim() -notmatch '^127\.0\.0\.1:(\d+)$'){throw 'Unexpected DB binding'}
  $postgresConnection='Host=127.0.0.1;Port='+$Matches[1]+';Database=narrative_test;Username=narrative_test;Password='+$password
 }
 Reservation
 $project=Join-Path $inputs 'Backend/ProjectRpg.Backend.NarrativeHistoryTests/ProjectRpg.Backend.NarrativeHistoryTests.csproj'
 Invoke-Child ('build "'+$project+'" --configuration Release --artifacts-path "'+$artifacts+'" -p:RestoreLockedMode=true -v minimal') 'Build.log'
 $test=Join-Path $artifacts 'bin/ProjectRpg.Backend.NarrativeHistoryTests/release/ProjectRpg.Backend.NarrativeHistoryTests.dll'
 $arguments='"'+$test+'"';if($Postgres){$arguments+=' --postgres'}
 Invoke-Child $arguments 'History.log'
 $legacy=Join-Path $inputs 'Backend/ProjectRpg.Backend.NarrativeTests/ProjectRpg.Backend.NarrativeTests.csproj'
 Invoke-Child ('build "'+$legacy+'" --configuration Release --artifacts-path "'+$artifacts+'" -p:RestoreLockedMode=true -v minimal') 'LegacyBuild.log'
 $legacyDll=Join-Path $artifacts 'bin/ProjectRpg.Backend.NarrativeTests/release/ProjectRpg.Backend.NarrativeTests.dll'
 Invoke-Child ('"'+$legacyDll+'"') 'LegacyNarrative.log'
 $kill=Join-Path $inputs 'Backend/ProjectRpg.Backend.NarrativeKillTests/ProjectRpg.Backend.NarrativeKillTests.csproj'
 Invoke-Child ('build "'+$kill+'" --configuration Release --artifacts-path "'+$artifacts+'" -p:RestoreLockedMode=true -v minimal') 'KillBuild.log'
 $killDll=Join-Path $artifacts 'bin/ProjectRpg.Backend.NarrativeKillTests/release/ProjectRpg.Backend.NarrativeKillTests.dll'
 $killArgs='"'+$killDll+'"';if($Postgres){$killArgs+=' --postgres'}
 Invoke-Child $killArgs 'KillRegression.log'
 foreach($input in $result.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $input.Path) -Algorithm SHA256).Hash -cne $input.SHA256){throw ('Verified source changed: '+$input.Path)}}
 if((Get-FileHash -LiteralPath $PSCommandPath).Hash -cne $result.RunnerSHA256){throw 'Runner changed during verification'}
 $result.InputsUnchanged=$true;$result.Passed=$true
} catch {$result.Failure=$_.Exception.Message;throw}
finally {
 try {
  $info=if($Postgres){Own-Container}else{$null}
  if($null -ne $info){& docker rm --force $info.Id|Out-Null;if($LASTEXITCODE -ne 0){throw 'Own DB cleanup failed'}}
  $result.ContainerDeleted=$true
  Reservation
  $resolved=[IO.Path]::GetFullPath($temp);$boundary=[IO.Path]::GetFullPath($run).TrimEnd('\')+'\'
  if(!$resolved.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolved) -cne 'Temp'){throw 'Unsafe own cleanup target'}
  $result.Cleanup=Remove-GuardianProbeArtifact -Path $resolved -AllowedRoot $run
  $result.TempDeleted=!(Test-Path -LiteralPath $resolved)
 } finally {$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $run 'Result.json') -Encoding UTF8}
 Write-Output ('Result: '+(Join-Path $run 'Result.json'))
}
