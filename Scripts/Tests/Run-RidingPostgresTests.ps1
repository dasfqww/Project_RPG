[CmdletBinding()]
param([string]$BaseCommit='HEAD')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[Guid]::NewGuid().ToString('N');$task='riding-postgres-20261113'
$root=Join-Path $workspace ('Saved/BuildChecks/RidingPostgres-20261113/'+$id)
$tempRoot=Join-Path $workspace 'Saved/RP';$temp=Join-Path $tempRoot $id.Substring(0,8);$repo=Join-Path $temp 'Repo'
$container=$null;$containerName='rpg-riding-'+$id
$docker=(Get-Command docker -ErrorAction SilentlyContinue).Source
if(!$docker){$docker=Join-Path $env:LOCALAPPDATA 'Programs/DockerDesktop/resources/bin/docker.exe'}
$files=@('Backend/ProjectRpg.Backend/Data/PostgresGameRepository.Riding.cs','Backend/ProjectRpg.Backend/Data/PostgresItemRepository.Riding.cs',
 'Backend/Database/Migrations/0015_riding_registration.sql','Backend/ProjectRpg.Backend/Data/PostgresSchemaMigrations.cs',
 'Backend/ProjectRpg.Backend/ProjectRpg.Backend.csproj','Backend/ProjectRpg.Backend.RidingPostgresTests/Program.cs',
 'Backend/ProjectRpg.Backend.RidingPostgresTests/ProjectRpg.Backend.RidingPostgresTests.csproj','Backend/ProjectRpg.Backend.MigrationTests/Program.cs')
$proof=[ordered]@{Passed=$false;Head='';Inputs=@();RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash;Commands=@();Tests=@();Modules=@{};Cleanup=$null;ContainerRemoved=$false;Failure=$null}
$connection='';$restart=Join-Path $temp 'Restart.json'
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Assert-Reservation {
 if(@([IO.File]::ReadAllLines((Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md'))|Where-Object{$_-match'^\|[^|]* / riding-postgres-20261113 \|'}).Count-ne1){throw 'Own reservation missing'}
}
function Run-Dotnet([string]$arguments,[string]$label) {
 $s=[Diagnostics.ProcessStartInfo]::new();$s.FileName=(Get-Command dotnet).Source;$s.Arguments=$arguments;$s.WorkingDirectory=$repo
 $s.UseShellExecute=$false;$s.CreateNoWindow=$true;$s.WindowStyle='Hidden';$s.RedirectStandardOutput=$true;$s.RedirectStandardError=$true
 foreach($key in @('POSTGRES_CONNECTION_STRING','PROJECT_RPG_BACKEND_ADMIN_TOKEN','STEAM_WEB_API_PUBLISHER_KEY')){$s.EnvironmentVariables.Remove($key)}
 $s.EnvironmentVariables['DOTNET_CLI_HOME']=Join-Path $temp 'cli';$s.EnvironmentVariables['NUGET_PACKAGES']=Join-Path $temp 'n'
 $s.EnvironmentVariables['NUGET_HTTP_CACHE_PATH']=Join-Path $temp 'http';$s.EnvironmentVariables['DOTNET_ENVIRONMENT']='Testing'
 $s.EnvironmentVariables['PROJECT_RPG_RIDING_TEST_POSTGRES']=$connection;$s.EnvironmentVariables['PROJECT_RPG_RIDING_RESTART_PROOF']=$restart
 $p=[Diagnostics.Process]::new();$p.StartInfo=$s
 try {
  if(!$p.Start()){throw 'Private dotnet failed to start'}
  $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$deadline=[DateTime]::UtcNow.AddMinutes(3)
  while(!$p.WaitForExit(500)){if([DateTime]::UtcNow-ge$deadline){$p.Kill();$p.WaitForExit();throw ('Private dotnet timeout: '+$label)}}
  $p.WaitForExit();$out=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult()
  [IO.File]::WriteAllText((Join-Path $root ($label+'.log')),$out+$err,[Text.UTF8Encoding]::new($false))
  $proof.Commands+=@{Label=$label;Exit=$p.ExitCode}
  if($p.ExitCode-ne0){throw ('Private dotnet failed: '+$label)}
  if($label-eq'MigrationPure'){
   $checks=[regex]::Matches($out,'(?m)^PASS Migration:').Count
   if($checks-lt3-or!$out.Contains('NOT RUN Migration: PostgreSQL integration')){throw 'Migration regression output incomplete'}
   $proof.MigrationPure=@{Passed=$true;Checks=$checks}
  }elseif($label-notmatch'Build'){$result=$out|ConvertFrom-Json;if(!$result.Passed){throw ('Failed tests: '+$label)};$proof.Tests+=@{Label=$label;Result=$result}}
 } finally {if($p.Id-and!$p.HasExited){$p.Kill();$p.WaitForExit()};$p.Dispose()}
}
try {
 Assert-Reservation
 $ancestor=$temp
 while($ancestor){if((Test-Path -LiteralPath $ancestor)-and((Get-Item -LiteralPath $ancestor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor'};$ancestor=[IO.Path]::GetDirectoryName($ancestor)}
 if(Test-Path -LiteralPath $temp){throw 'Temporary path exists'}
 $null=[IO.Directory]::CreateDirectory($repo);$null=[IO.Directory]::CreateDirectory($root)
 $proof.Head=(git -C $workspace rev-parse --verify ($BaseCommit+'^{commit}')).Trim()
 if($LASTEXITCODE){throw 'Invalid immutable base'}
 $archive=Join-Path $temp 'Backend.tar';git -C $workspace archive --format=tar ('--output='+$archive) $proof.Head -- Backend
 if($LASTEXITCODE){throw 'Archive failed'}
 tar -xf $archive -C $repo;if($LASTEXITCODE){throw 'Extraction failed'}
 foreach($file in $files){$source=Join-Path $workspace $file;$destination=Join-Path $repo $file;$hash=(Get-FileHash -LiteralPath $source).Hash
  $null=[IO.Directory]::CreateDirectory((Split-Path $destination));Copy-Item -LiteralPath $source -Destination $destination
  if((Get-FileHash -LiteralPath $destination).Hash-ne$hash-or(Get-FileHash -LiteralPath $source).Hash-ne$hash){throw 'Overlay changed'};$proof.Inputs+=@{Path=$file;SHA256=$hash}
 }
 $password=[Guid]::NewGuid().ToString('N')
 $container=& $docker run --pull=never --detach --name $containerName --label ('project-rpg.task='+$task) --label ('project-rpg.run='+$id) --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=268435456' --env 'POSTGRES_DB=riding_test' --env 'POSTGRES_USER=riding_test' --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine
 if($LASTEXITCODE){$container=$null;throw 'Private PostgreSQL did not start'};$container=$container.Trim()
 $ready=$false
 for($i=0;$i-lt60;$i++){& $docker exec $container pg_isready -h 127.0.0.1 -U riding_test -d riding_test *> $null;if(!$LASTEXITCODE){$ready=$true;break};Start-Sleep -Milliseconds 500}
 if(!$ready){throw 'Private PostgreSQL not ready'}
 $port=(& $docker port $container 5432/tcp).Trim()
 if($LASTEXITCODE-or$port-notmatch'^127\.0\.0\.1:(\d+)$'){throw 'Private database must bind loopback only'}
 $connection='Host=127.0.0.1;Port='+$Matches[1]+';Database=riding_test;Username=riding_test;Password='+$password+';Timeout=10;Command Timeout=20;Maximum Pool Size=40'
 $project=Join-Path $repo 'Backend/ProjectRpg.Backend.RidingPostgresTests/ProjectRpg.Backend.RidingPostgresTests.csproj'
 Run-Dotnet ('build "'+$project+'" -c Release --nologo --disable-build-servers -p:UseSharedCompilation=false') 'PostgresBuild'
 $dll=Join-Path (Split-Path $project) 'bin/Release/net9.0/ProjectRpg.Backend.RidingPostgresTests.dll'
 $proof.Modules.PostgresTests=(Get-FileHash -LiteralPath $dll).Hash
 Run-Dotnet ('"'+$dll+'"') 'PostgresTests'
 Run-Dotnet ('"'+$dll+'" --verify-restart') 'RestartTests'
 $old=Join-Path $repo 'Backend/ProjectRpg.Backend.RidingTests/ProjectRpg.Backend.RidingTests.csproj'
 Run-Dotnet ('build "'+$old+'" -c Release --nologo --disable-build-servers -p:UseSharedCompilation=false') 'MemoryBuild'
 $memory=Join-Path (Split-Path $old) 'bin/Release/net9.0/ProjectRpg.Backend.RidingTests.dll'
 $proof.Modules.MemoryTests=(Get-FileHash -LiteralPath $memory).Hash
 Run-Dotnet ('"'+$memory+'" --memory') 'MemoryTests'
 $migration=Join-Path $repo 'Backend/ProjectRpg.Backend.MigrationTests/ProjectRpg.Backend.MigrationTests.csproj'
 Run-Dotnet ('build "'+$migration+'" -c Release --nologo --disable-build-servers -p:UseSharedCompilation=false') 'MigrationBuild'
 Run-Dotnet ('"'+(Join-Path (Split-Path $migration) 'bin/Release/net9.0/ProjectRpg.Backend.MigrationTests.dll')+'"') 'MigrationPure'
 if($proof.Tests[0].Result.CaseCount-ne13-or$proof.Tests[1].Result.CaseCount-ne1-or$proof.Tests[2].Result.CaseCount-ne27){throw 'Missing expected test cases'}
 foreach($f in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $f.Path)).Hash-ne$f.SHA256){throw 'Input changed during verification'}}
 if((Get-FileHash -LiteralPath $PSCommandPath).Hash-ne$proof.RunnerSHA256){throw 'Runner changed during verification'}
 $proof.Passed=$true
} catch {$proof.Failure=$_.Exception.Message}
finally {
 try {
  Assert-Reservation
  if($container){$info=(& $docker inspect $container|ConvertFrom-Json)[0]
   if($LASTEXITCODE-or$info.Config.Labels.'project-rpg.task'-ne$task-or$info.Config.Labels.'project-rpg.run'-ne$id-or$info.Name-ne('/'+$containerName)){throw 'Container ownership mismatch'}
   & $docker rm --force $container *> $null;if($LASTEXITCODE){throw 'Private container cleanup failed'}
  }
  $proof.ContainerRemoved=$true
  if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}
 } catch {$proof.Failure=([string]$proof.Failure+'; cleanup: '+$_.Exception.Message)}
 $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
 $proof.Passed=$proof.Passed-and!$proof.Failure-and$proof.CleanupComplete-and$proof.ContainerRemoved
 $proof.HistoricalHashesOnly=$true;$null=[IO.Directory]::CreateDirectory($root)
 [IO.File]::WriteAllText((Join-Path $root 'Result.json'),($proof|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))
 Write-Output ('Passed='+$proof.Passed+'; proof='+$root)
}
if(!$proof.Passed){throw $proof.Failure}
