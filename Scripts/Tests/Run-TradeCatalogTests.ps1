[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runId=[Guid]::NewGuid().ToString('N');$task='trading-item-catalog-20261114'
$proofRoot=Join-Path $workspace 'Saved/BuildChecks/TradeCatalog-20261114';$proof=Join-Path $proofRoot $runId
$allowed=Join-Path $workspace 'Saved/TC';$temp=Join-Path $allowed $runId.Substring(0,8);$inputs=Join-Path $temp 'Inputs'
$container=$null;$containerName='rpg-trade-catalog-'+$runId;$ownsTemp=$false
$paths=@('Backend/ProjectRpg.Backend/Configuration/TradeItemCatalogConfiguration.cs',
 'Backend/ProjectRpg.Backend/Program.cs','Backend/ProjectRpg.Backend/appsettings.json',
 'Backend/ProjectRpg.Backend.TradingTests/Program.cs','Backend/ProjectRpg.Backend.TradingTests/CatalogCases.cs',
 'Scripts/Tests/Run-TradeCatalogTests.ps1')
$names=@('PROJECT_RPG_CATALOG_TEST_POSTGRES','PROJECT_RPG_CATALOG_TEST_RUN','Kestrel__Endpoints__Http__Url','PROJECT_RPG_TRADE_TEST_RUN')
$saved=@{};foreach($name in $names){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
$result=[ordered]@{Passed=$false;Run=$runId;SnapshotCommit=$null;Inputs=@();Commands=@();ModuleSHA256=$null;BackendModuleSHA256=$null;Cleanup=$null;CleanupComplete=$false;Failure=$null}
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Reserve {if(@(Get-Content (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object{$_ -match '^\|[^|]* / trading-item-catalog-20261114 \|'}).Count -ne 1){throw 'Own reservation missing'}}
function Save {[IO.File]::WriteAllText((Join-Path $proof 'Result.json'),($result|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
function Invoke-Dotnet([string[]]$Arguments,[string]$Label){
 $oldPreference=$ErrorActionPreference
 try{$ErrorActionPreference='Continue';& dotnet @Arguments *> (Join-Path $proof ($Label+'.log'));$code=$LASTEXITCODE}finally{$ErrorActionPreference=$oldPreference}
 $result.Commands+=@{Label=$Label;Command=@('dotnet')+$Arguments;Exit=$code};Save
 Get-Content -LiteralPath (Join-Path $proof ($Label+'.log')) -Tail 8
 if($code -ne 0){throw ('dotnet failed: '+$Label)}
}
try{
 Reserve;$null=[IO.Directory]::CreateDirectory($allowed);$null=Assert-GuardianArtifactPath $temp $allowed
 if(Test-Path -LiteralPath $temp){throw 'Own temporary directory already exists'}
 $null=[IO.Directory]::CreateDirectory($inputs);$ownsTemp=$true;$null=[IO.Directory]::CreateDirectory($proof)
 $result.SnapshotCommit=(git -C $workspace rev-parse HEAD).Trim();if($LASTEXITCODE){throw 'Cannot resolve snapshot'}
 & git -C $workspace archive --format=tar -o (Join-Path $temp 'Backend.tar') $result.SnapshotCommit Backend;if($LASTEXITCODE){throw 'Snapshot archive failed'}
 & tar -xf (Join-Path $temp 'Backend.tar') -C $inputs;if($LASTEXITCODE){throw 'Snapshot extraction failed'}
 foreach($path in $paths){$source=Join-Path $workspace $path;$destination=Join-Path $inputs $path;$hash=(Get-FileHash -LiteralPath $source).Hash;$null=[IO.Directory]::CreateDirectory((Split-Path $destination));Copy-Item -LiteralPath $source -Destination $destination;if((Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw ('Input changed during capture '+$path)};$result.Inputs+=@{Path=$path;SHA256=$hash}}
 $result.BackendTree=(git -C $workspace rev-parse ($result.SnapshotCommit+':Backend')).Trim();Save
 $project=Join-Path $inputs 'Backend/ProjectRpg.Backend.TradingTests/ProjectRpg.Backend.TradingTests.csproj'
 Invoke-Dotnet @('build',$project,'-c','Release','--disable-build-servers','-p:UseSharedCompilation=false') 'build'
 $dll=Join-Path $inputs 'Backend/ProjectRpg.Backend.TradingTests/bin/Release/net9.0/ProjectRpg.Backend.TradingTests.dll'
 $result.ModuleSHA256=(Get-FileHash -LiteralPath $dll).Hash;$result.BackendModuleSHA256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $dll) 'ProjectRpg.Backend.dll')).Hash
 $docker=Join-Path $env:LOCALAPPDATA 'Programs/DockerDesktop/resources/bin/docker.exe';$password=[Guid]::NewGuid().ToString('N')
 $container=& $docker run --pull=never --detach --name $containerName --label ('project-rpg.task='+$task) --label ('project-rpg.run='+$runId) --publish '127.0.0.1::5432' --tmpfs '/var/lib/postgresql/data:rw,size=268435456' --env 'POSTGRES_DB=trade_catalog_test' --env 'POSTGRES_USER=catalog_test' --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine
 if($LASTEXITCODE){$container=$null;throw 'Private PostgreSQL start failed'};$container=$container.Trim()
 $ready=$false;for($i=0;$i -lt 50;$i++){& $docker exec $container pg_isready -h 127.0.0.1 -U catalog_test -d trade_catalog_test *> $null;if(!$LASTEXITCODE){$ready=$true;break};Start-Sleep -Milliseconds 300};if(!$ready){throw 'Private PostgreSQL not ready'}
 $port=(& $docker port $container 5432/tcp).Trim().Split(':')[-1]
 $env:PROJECT_RPG_CATALOG_TEST_POSTGRES='Host=127.0.0.1;Port='+$port+';Username=catalog_test;Password='+$password+';Database=trade_catalog_test'
 $env:PROJECT_RPG_CATALOG_TEST_RUN=$runId;$env:PROJECT_RPG_TRADE_TEST_RUN=$runId;$env:Kestrel__Endpoints__Http__Url='http://127.0.0.1:0'
 Invoke-Dotnet @($dll,'--item-catalog') 'catalog'
 foreach($mode in @('--api-memory','--auction-api-memory','--auction-domain','')){$label=if($mode){$mode.TrimStart('-')}else{'direct-domain'};$arguments=@($dll);if($mode){$arguments+=$mode};Invoke-Dotnet $arguments $label}
 foreach($entry in $result.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash -ne $entry.SHA256){throw ('Input changed after verification '+$entry.Path)}}
 $result.Passed=$true
}catch{$result.Failure=$_.Exception.Message}
finally{
 try{
  Reserve
  $active=@(Get-CimInstance Win32_Process|Where-Object {$_.Name -eq 'dotnet.exe' -and $_.CommandLine -and $_.CommandLine.Replace('/','\').Contains($temp.Replace('/','\'))});if($active.Count){throw 'Own backend child is still using temporary files'}
  if($container){$info=(& $docker inspect $container|ConvertFrom-Json)[0];if($info.Id -ne $container -or $info.Name -ne ('/'+$containerName) -or $info.Config.Labels.'project-rpg.task' -ne $task -or $info.Config.Labels.'project-rpg.run' -ne $runId){throw 'PG ownership mismatch'};& $docker rm --force $container|Out-Null;if($LASTEXITCODE){throw 'Private PG cleanup failed'}}
  if($ownsTemp -and (Test-Path -LiteralPath $temp)){$result.Cleanup=Remove-GuardianProbeArtifact $temp $allowed}
 }catch{$result.Failure=([string]$result.Failure+'; cleanup: '+$_.Exception.Message)}
 foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
 $result.CleanupComplete=!(Test-Path -LiteralPath $temp);$result.HistoricalHashesOnly=$true;$result.Passed=$result.Passed -and !$result.Failure -and $result.CleanupComplete;Save
 Write-Output ('Passed='+$result.Passed+'; '+$proof)
}
if(!$result.Passed){throw $result.Failure}
