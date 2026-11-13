[CmdletBinding()]
param([string]$BaseCommit='HEAD')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId=[Guid]::NewGuid().ToString('N');$run=Join-Path $workspace ('Saved/BuildChecks/NarrativeMarker-20261113/Backend-'+$runId)
$allowed=Join-Path $workspace 'Saved/NMK';$temp=Join-Path $allowed $runId.Substring(0,8);$inputs=Join-Path $temp 'Repo';$artifacts=Join-Path $temp 'Artifacts'
$result=[ordered]@{Passed=$false;Base=$null;Inputs=@();RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash;Cleanup=$null;TempDeleted=$false;Failure=$null}
$ownsTemp=$false
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Reserve {if(@(Get-Content -Encoding UTF8 (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md')|Where-Object{$_ -match '^\|[^|]* / narrative-marker-20261113 \|'}).Count -ne 1){throw 'Reservation missing'}}
function Save {[IO.File]::WriteAllText((Join-Path $run 'Result.json'),($result|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))}
function Child([string[]]$Arguments,[string]$Log){
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=(Get-Command dotnet).Source;$start.Arguments=$Arguments -join ' '
 $start.WorkingDirectory=$inputs;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle='Hidden';$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 foreach($name in @('POSTGRES_CONNECTION_STRING','PROJECT_RPG_BACKEND_ADMIN_TOKEN','PROJECT_RPG_NARRATIVE_TEST_POSTGRES','PROJECT_RPG_NARRATIVE_TEST_RUN')){$start.EnvironmentVariables.Remove($name)}
 $proc=[Diagnostics.Process]::new();$proc.StartInfo=$start
 try {if(!$proc.Start()){throw 'Owned process failed to start'};$stdout=$proc.StandardOutput.ReadToEndAsync();$stderr=$proc.StandardError.ReadToEndAsync()
  if(!$proc.WaitForExit(180000)){$proc.Kill();$proc.WaitForExit();throw 'Owned backend child timed out'}
  [IO.File]::WriteAllText((Join-Path $run $Log),($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()),[Text.UTF8Encoding]::new($false))
  if($proc.ExitCode -ne 0){throw ('Backend check failed: '+$Log)};Write-Output ('PASS '+$Log)
 }finally{if(!$proc.HasExited){$proc.Kill();$proc.WaitForExit()};$proc.Dispose()}
}
try {
 Reserve;if(Test-Path -LiteralPath $temp){throw 'Own path collision'}
 $null=[IO.Directory]::CreateDirectory($inputs);$null=[IO.Directory]::CreateDirectory($run);$ownsTemp=$true
 $result.Base=(git -C $workspace rev-parse ($BaseCommit+'^{commit}')).Trim();if($LASTEXITCODE){throw 'Immutable base absent'}
 & git -C $workspace archive --format=tar ('-o'+(Join-Path $temp 'Source.tar')) $result.Base -- Backend
 if($LASTEXITCODE){throw 'Archive failed'};& tar -xf (Join-Path $temp 'Source.tar') -C $inputs;if($LASTEXITCODE){throw 'Extraction failed'}
 foreach($path in @('Backend/ProjectRpg.Backend/Narrative/NarrativeTypes.cs','Backend/ProjectRpg.Backend/Narrative/DialogueManager.cs','Backend/ProjectRpg.Backend/Narrative/NarrativeCatalogLoader.cs',
 'Backend/ProjectRpg.Backend.NarrativeMarkerTests/ProjectRpg.Backend.NarrativeMarkerTests.csproj','Backend/ProjectRpg.Backend.NarrativeMarkerTests/Program.cs')){
  $from=Join-Path $workspace $path;$to=Join-Path $inputs $path;$hash=(Get-FileHash -LiteralPath $from).Hash
  $null=[IO.Directory]::CreateDirectory((Split-Path $to));Copy-Item -LiteralPath $from -Destination $to
  if((Get-FileHash -LiteralPath $to).Hash -ne $hash -or (Get-FileHash -LiteralPath $from).Hash -ne $hash){throw 'Overlay changed'}
  $result.Inputs+=@{Path=$path;SHA256=$hash}
 }
 foreach($name in @('NarrativeMarker','Narrative','NarrativeSkip')){
  Reserve;$project=Join-Path $inputs ('Backend/ProjectRpg.Backend.'+$name+'Tests/ProjectRpg.Backend.'+$name+'Tests.csproj')
  Child @('build',('"'+$project+'"'),'-c','Release','--artifacts-path',('"'+$artifacts+'"'),'-p:RestoreLockedMode=true','--nologo') ($name+'Build.log')
  $dll=Join-Path $artifacts ('bin/ProjectRpg.Backend.'+$name+'Tests/release/ProjectRpg.Backend.'+$name+'Tests.dll')
  $argsList=@(('"'+$dll+'"'));if($name -eq 'NarrativeMarker'){$argsList+=('"'+(Join-Path $run 'HttpWire.json')+'"')}
  Child $argsList ($name+'.log')
 }
 $wire=Join-Path $run 'HttpWire.json';$result.HttpWire=@{Path=$wire;SHA256=(Get-FileHash -LiteralPath $wire).Hash}
 foreach($entry in $result.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash -ne $entry.SHA256){throw 'Verified input changed'}}
 if((Get-FileHash -LiteralPath $PSCommandPath).Hash -ne $result.RunnerSHA256){throw 'Runner changed'}
 $result.Passed=$true
}catch{$result.Failure=$_.Exception.Message}
finally {
 try {Reserve;if($ownsTemp -and (Test-Path -LiteralPath $temp)){$result.Cleanup=Remove-GuardianProbeArtifact -Path $temp -AllowedRoot $allowed}}
 catch{$result.Failure=([string]$result.Failure+'; cleanup: '+$_.Exception.Message)}
 $result.TempDeleted=!(Test-Path -LiteralPath $temp);$result.Passed=$result.Passed -and !$result.Failure -and $result.TempDeleted;Save
 Write-Output ('Passed='+$result.Passed+'; proof='+$run)
}
if(!$result.Passed){throw $result.Failure}
