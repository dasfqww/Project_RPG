[CmdletBinding()]
param([string]$BaseCommit='HEAD')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId=[Guid]::NewGuid().ToString('N')
$root=Join-Path $workspace ('Saved/BuildChecks/RidingDomain-20261113/'+$runId)
$tempRoot=Join-Path $workspace 'Saved/RD'
$temp=Join-Path $tempRoot $runId.Substring(0,8)
$repo=Join-Path $temp 'Repo'
$proof=[ordered]@{Passed=$false;Scope='Riding catalog/registration proposal and real Item V2 record rules; no persistence/API/auth/PG/UE';Head='';Inputs=@();BuildExit=$null;TestExit=$null;Tests=$null;Cleanup=$null;Failure=$null}
$files=@('Backend/ProjectRpg.Backend/Riding/RidingIdentity.cs','Backend/ProjectRpg.Backend/Riding/RidingCatalog.cs',
 'Backend/ProjectRpg.Backend/Riding/RidingRegistration.cs','Backend/ProjectRpg.Backend.RidingTests/Program.cs',
 'Backend/ProjectRpg.Backend.RidingTests/ProjectRpg.Backend.RidingTests.csproj')
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Assert-Reservation {
 if(@([IO.File]::ReadAllLines((Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md'))|
  Where-Object{$_-match'^\|[^|]* / riding-domain-20261113 \|'}).Count-ne1){throw 'Own reservation missing'}
}
function Save-Proof {
 $null=[IO.Directory]::CreateDirectory($root)
 [IO.File]::WriteAllText((Join-Path $root 'Result.json'),($proof|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
}
function Invoke-OwnDotnet([string]$Arguments,[string]$LogName) {
 $start=[Diagnostics.ProcessStartInfo]::new()
 $start.FileName=(Get-Command dotnet -ErrorAction Stop).Source;$start.Arguments=$Arguments
 $start.WorkingDirectory=$repo;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle='Hidden'
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 foreach($key in @('POSTGRES_CONNECTION_STRING','PROJECT_RPG_BACKEND_ADMIN_TOKEN','STEAM_WEB_API_PUBLISHER_KEY',
  'PROJECT_RPG_TRADING_TEST_POSTGRES','PROJECT_RPG_NARRATIVE_TEST_POSTGRES')){$start.EnvironmentVariables.Remove($key)}
 $start.EnvironmentVariables['DOTNET_CLI_HOME']=Join-Path $temp 'CliHome'
 $start.EnvironmentVariables['NUGET_PACKAGES']=Join-Path $temp 'n'
 $start.EnvironmentVariables['NUGET_HTTP_CACHE_PATH']=Join-Path $temp 'HttpCache'
 $start.EnvironmentVariables['DOTNET_ENVIRONMENT']='Testing'
 $start.EnvironmentVariables['ASPNETCORE_ENVIRONMENT']='Testing'
 $child=[Diagnostics.Process]::new();$child.StartInfo=$start
 try {
  if(!$child.Start()){throw 'Own dotnet process did not start'}
  $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
  $deadline=[DateTime]::UtcNow.AddMinutes(3)
  while(!$child.WaitForExit(500)){if([DateTime]::UtcNow-ge$deadline){$child.Kill();$child.WaitForExit();throw 'Own dotnet process timed out'}}
  $child.WaitForExit()
  [IO.File]::WriteAllText((Join-Path $root $LogName),$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
  return $child.ExitCode
 } finally {if($child.Id-and!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
}
try {
 Assert-Reservation
 $ancestor=$temp
 while($ancestor){if((Test-Path -LiteralPath $ancestor)-and((Get-Item -LiteralPath $ancestor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor rejected'};$ancestor=[IO.Path]::GetDirectoryName($ancestor)}
 if(Test-Path -LiteralPath $temp){throw 'Temporary path already exists'}
 $null=[IO.Directory]::CreateDirectory($repo)
 $proof.Head=(git -C $workspace rev-parse --verify ($BaseCommit+'^{commit}')).Trim()
 if($LASTEXITCODE-ne0-or$proof.Head-notmatch'^[a-f0-9]{40}$'){throw 'Invalid immutable base'}
 $archive=Join-Path $temp 'Backend.tar'
 git -C $workspace archive --format=tar ('--output='+$archive) $proof.Head -- Backend
 if($LASTEXITCODE){throw 'Backend snapshot failed'}
 tar -xf $archive -C $repo
 if($LASTEXITCODE){throw 'Backend extraction failed'}
 foreach($path in $files){
  $source=Join-Path $workspace $path;$destination=Join-Path $repo $path;$hash=(Get-FileHash -LiteralPath $source).Hash
  $null=[IO.Directory]::CreateDirectory((Split-Path $destination));Copy-Item -LiteralPath $source -Destination $destination
  if((Get-FileHash -LiteralPath $destination).Hash-ne$hash-or(Get-FileHash -LiteralPath $source).Hash-ne$hash){throw 'Overlay changed during capture'}
  $proof.Inputs+=@{Path=$path;SHA256=$hash}
 }
 $project=Join-Path $repo 'Backend/ProjectRpg.Backend.RidingTests/ProjectRpg.Backend.RidingTests.csproj'
 $dll=Join-Path $repo 'Backend/ProjectRpg.Backend.RidingTests/bin/Release/net9.0/ProjectRpg.Backend.RidingTests.dll'
 Save-Proof;Write-Output ('Building isolated Riding domain; proof: '+$root)
 $proof.BuildExit=Invoke-OwnDotnet ('build "'+$project+'" -c Release --nologo --disable-build-servers -p:UseSharedCompilation=false') 'Build.log'
 if($proof.BuildExit-ne0){throw 'Riding backend/test build failed'}
 $proof.ModuleSHA256=(Get-FileHash -LiteralPath $dll).Hash
 $proof.TestExit=Invoke-OwnDotnet ('"'+$dll+'"') 'Tests.log'
 if($proof.TestExit-ne0){throw 'Riding domain tests failed'}
 $proof.Tests=[IO.File]::ReadAllText((Join-Path $root 'Tests.log'))|ConvertFrom-Json
 $expected=@('canonical_order_and_culture','catalog_defensive_snapshot','catalog_compatibility','catalog_shape_and_limits',
  'catalog_mapping_exclusivity','policy_bounds_match_ue','registration_stack_plan','registration_tombstone_and_snapshot',
  'already_owned_is_noop','registration_inventory_and_availability','registration_fail_closed_shapes_and_limits','command_fingerprint_and_child_identity')
 if(!$proof.Tests.Passed-or(Compare-Object $expected @($proof.Tests.Cases.Name))-or@($proof.Tests.Cases|Where-Object{!$_.Passed-or$_.Assertions-le0}).Count){throw 'Missing or failing domain cases'}
 foreach($f in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $f.Path)).Hash-ne$f.SHA256){throw 'Original input changed'}}
 $proof.Passed=$true
} catch {$proof.Failure=$_.Exception.Message}
finally {
 try {Assert-Reservation;if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}}
 catch {$proof.Failure=$_.Exception.Message}
 $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
 $proof.Passed=$proof.Passed-and!$proof.Failure-and$proof.CleanupComplete
 $proof.HistoricalHashesOnly=$true;Save-Proof
 Write-Output ('Passed='+$proof.Passed+'; proof='+$root)
}
if(!$proof.Passed){throw $proof.Failure}
