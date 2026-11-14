[CmdletBinding()]
param([switch]$IncludePostgres)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$id=[Guid]::NewGuid().ToString('N');$task='riding-registration-options-20261114'
$root=Join-Path $workspace ('Saved/BuildChecks/RidingRegistrationOptions-20261114/'+$id)
$tempRoot=Join-Path $workspace 'Saved/RQ';$temp=Join-Path $tempRoot $id.Substring(0,8);$repo=Join-Path $temp 'Repo'
$container=$null;$containerName='rpg-riding-options-'+$id;$connection='';$ownsTemp=$false
$docker=(Get-Command docker -ErrorAction SilentlyContinue).Source
if(!$docker){$docker=Join-Path $env:LOCALAPPDATA 'Programs/DockerDesktop/resources/bin/docker.exe'}
$proof=[ordered]@{Passed=$false;Head='';Inputs=@();RunnerSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash;Commands=@();Tests=@();Modules=@{};PostgresRequested=[bool]$IncludePostgres;Cleanup=$null;ContainerRemoved=$false;Failure=$null}
$files=@('Backend/ProjectRpg.Backend/Api/RidingApiEndpoints.cs','Backend/ProjectRpg.Backend/Api/RidingRegistrationOptionsApi.cs',
    'Backend/ProjectRpg.Backend.RidingHttpTests/Program.cs','Backend/ProjectRpg.Backend.RidingHttpTests/RegistrationOptionsCases.cs')
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Save-Proof { if(Test-Path -LiteralPath $root){[IO.File]::WriteAllText((Join-Path $root 'Result.json'),($proof|ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($false))} }
function Assert-Reservation {
    if(@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8|Where-Object{$_-match'^\|[^|]* / riding-registration-options-20261114 \|'}).Count-ne1){throw 'Own reservation missing'}
}
function Run-Dotnet([string]$Arguments,[string]$Label) {
    $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=(Get-Command dotnet).Source;$start.Arguments=$Arguments
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WorkingDirectory=$repo;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.EnvironmentVariables['DOTNET_CLI_HOME']=Join-Path $temp 'Cli'
    $start.EnvironmentVariables['NUGET_PACKAGES']=Join-Path $temp 'Nuget'
    $start.EnvironmentVariables['NUGET_HTTP_CACHE_PATH']=Join-Path $temp 'NugetHttp'
    $start.EnvironmentVariables['PROJECT_RPG_TEST_POSTGRES']=$connection
    $child=[Diagnostics.Process]::new();$child.StartInfo=$start
    try {
        if(!$child.Start()){throw 'Private dotnet start failed'}
        $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync();$deadline=[DateTime]::UtcNow.AddMinutes(4)
        while(!$child.WaitForExit(250)){if([DateTime]::UtcNow-ge$deadline){$child.Kill();$child.WaitForExit();throw ('Private dotnet timeout: '+$Label)}}
        $child.WaitForExit();$out=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $root ($Label+'.log')),$out+$err,[Text.UTF8Encoding]::new($false))
        $proof.Commands+=@{Label=$Label;Arguments=$Arguments;Exit=$child.ExitCode}
        if($child.ExitCode-ne0){throw ('Private dotnet failed: '+$Label)}
        if($Label-ne'Build'){$result=$out|ConvertFrom-Json;if(!$result.Passed){throw ('Failed cases: '+$Label)};$proof.Tests+=@{Label=$Label;Result=$result}}
    } finally {if($child.Id-and!$child.HasExited){$child.Kill();$child.WaitForExit()};$child.Dispose()}
}
try {
    Assert-Reservation
    $ancestor=$temp
    while($ancestor){if((Test-Path -LiteralPath $ancestor)-and((Get-Item -LiteralPath $ancestor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'Redirected temporary ancestor'};$ancestor=[IO.Path]::GetDirectoryName($ancestor)}
    if(Test-Path -LiteralPath $temp){throw 'Own temporary directory already exists'}
    $null=[IO.Directory]::CreateDirectory($repo);$ownsTemp=$true;$null=[IO.Directory]::CreateDirectory($root)
    $proof.Head=(git -C $workspace rev-parse HEAD).Trim()
    $proof.BackendTree=(git -C $workspace rev-parse ($proof.Head+':Backend')).Trim()
    & git -C $workspace archive --format=tar ('--output='+(Join-Path $temp 'Backend.tar')) $proof.Head -- Backend
    if($LASTEXITCODE){throw 'Backend archive failed'}
    & tar -xf (Join-Path $temp 'Backend.tar') -C $repo
    if($LASTEXITCODE){throw 'Backend extraction failed'}
    foreach($relative in $files) {
        $source=Join-Path $workspace $relative;$target=Join-Path $repo $relative;$hash=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path -Parent $target));Copy-Item -LiteralPath $source -Destination $target
        if((Get-FileHash -LiteralPath $source).Hash-ne$hash-or(Get-FileHash -LiteralPath $target).Hash-ne$hash){throw 'Input changed during capture'}
        $proof.Inputs+=@{Path=$relative;SHA256=$hash}
    }
    if($IncludePostgres) {
        if(!(Test-Path -LiteralPath $docker)){throw 'Optional PostgreSQL requires an existing Docker engine'}
        & $docker info --format '{{.ServerVersion}}' *> $null
        if($LASTEXITCODE){throw 'Optional PostgreSQL requires a running Docker engine; no global daemon was started'}
        $password=[Guid]::NewGuid().ToString('N')
        $container=& $docker run --detach --rm --name $containerName --label ('project-rpg.task='+$task) --label ('project-rpg.run='+$id) --publish '127.0.0.1::5432' --env 'POSTGRES_DB=riding_options' --env 'POSTGRES_USER=riding_options' --env ('POSTGRES_PASSWORD='+$password) postgres:17-alpine
        if($LASTEXITCODE){$container=$null;throw 'Private PostgreSQL start failed'};$container=$container.Trim()
        $ready=$false
        for($n=0;$n-lt60;++$n){& $docker exec $container pg_isready -h 127.0.0.1 -U riding_options -d riding_options *> $null;if(!$LASTEXITCODE){$ready=$true;break};Start-Sleep -Milliseconds 500}
        if(!$ready){throw 'Private PostgreSQL not ready'}
        $port=(& $docker port $container 5432/tcp).Trim();if($LASTEXITCODE-or$port-notmatch'^127\.0\.0\.1:(\d+)$'){throw 'Private PostgreSQL must bind loopback'}
        $connection='Host=127.0.0.1;Port='+$Matches[1]+';Database=riding_options;Username=riding_options;Password='+$password+';Timeout=10;Command Timeout=20'
    }
    $project=Join-Path $repo 'Backend/ProjectRpg.Backend.RidingHttpTests/ProjectRpg.Backend.RidingHttpTests.csproj'
    Save-Proof
    Run-Dotnet ('build "'+$project+'" -c Release --nologo --disable-build-servers -p:UseSharedCompilation=false') 'Build'
    $dll=Join-Path (Split-Path -Parent $project) 'bin/Release/net9.0/ProjectRpg.Backend.RidingHttpTests.dll'
    $proof.Modules.HttpTests=(Get-FileHash -LiteralPath $dll).Hash
    $proof.Modules.Backend=(Get-FileHash -LiteralPath (Join-Path (Split-Path -Parent $dll) 'ProjectRpg.Backend.dll')).Hash
    Run-Dotnet ('"'+$dll+'" --registration-options') 'RegistrationOptionsMemory'
    Run-Dotnet ('"'+$dll+'"') 'ExistingRegistrationMemory'
    Run-Dotnet ('"'+$dll+'" --preferences') 'ExistingPreferencesMemory'
    if($IncludePostgres){Run-Dotnet ('"'+$dll+'" --registration-options-postgres') 'RegistrationOptionsPostgres'}
    $expected=@{RegistrationOptionsMemory=7;ExistingRegistrationMemory=7;ExistingPreferencesMemory=8}
    if($IncludePostgres){$expected.RegistrationOptionsPostgres=7}
    if($proof.Tests.Count-ne$expected.Count){throw 'Missing expected suites'}
    foreach($test in $proof.Tests){if($test.Result.CaseCount-ne$expected[$test.Label]){throw ('Missing cases: '+$test.Label)}}
    foreach($entry in $proof.Inputs){if((Get-FileHash -LiteralPath (Join-Path $workspace $entry.Path)).Hash-ne$entry.SHA256){throw 'Input changed during validation'}}
    if((Get-FileHash -LiteralPath $PSCommandPath).Hash-ne$proof.RunnerSHA256){throw 'Runner changed during validation'}
    $proof.Passed=$true
} catch {$proof.Failure=$_.Exception.Message}
finally {
    $released=$true
    try {
        Assert-Reservation
        if($container){
            $info=(& $docker inspect $container|ConvertFrom-Json)[0]
            if($LASTEXITCODE-or$info.Config.Labels.'project-rpg.task'-ne$task-or$info.Config.Labels.'project-rpg.run'-ne$id-or$info.Name-ne('/'+$containerName)){throw 'Container ownership mismatch'}
            & $docker rm --force $container *> $null;if($LASTEXITCODE){throw 'Private container cleanup failed'}
        }
        $proof.ContainerRemoved=$true
    } catch {$released=$false;$proof.Failure=([string]$proof.Failure+'; cleanup: '+$_.Exception.Message)}
    if($released-and$ownsTemp-and(Test-Path -LiteralPath $temp)){try{$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}catch{$proof.Failure=([string]$proof.Failure+'; cleanup: '+$_.Exception.Message)}}
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp);$proof.Passed=$proof.Passed-and!$proof.Failure-and$proof.CleanupComplete-and$proof.ContainerRemoved
    $proof.HistoricalHashesOnly=$true;Save-Proof;Write-Output ('Passed='+$proof.Passed+'; proof='+$root)
}
if(!$proof.Passed){throw $proof.Failure}
