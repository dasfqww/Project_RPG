[CmdletBinding()]
param([string]$PostgresConnectionString = $env:PROJECT_RPG_ABANDON_TEST_POSTGRES, [switch]$RequirePostgres)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$root = Join-Path $projectRoot ('Saved/Tests/DungeonDeparture/' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Force -Path $root
$build = Join-Path $root 'build'
$dotnet = (Get-Command dotnet).Source
$paths = @('Backend/Database/schema.sql', 'Backend/ProjectRpg.Backend/Domain/DomainModels.cs',
    'Backend/ProjectRpg.Backend/Domain/DungeonDeparturePolicy.cs', 'Backend/ProjectRpg.Backend/Data/IGameRepository.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.cs', 'Backend/ProjectRpg.Backend/Data/PostgresGameRepository.cs',
    'Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.Departure.cs', 'Backend/ProjectRpg.Backend/Data/PostgresGameRepository.Departure.cs',
    'Backend/ProjectRpg.Backend/Api/DungeonDepartureApiEndpoints.cs', 'Backend/ProjectRpg.Backend/Contracts/ApiContracts.cs',
    'Backend/ProjectRpg.Backend/Contracts/DungeonSessionResponses.cs', 'Backend/ProjectRpg.Backend/Program.cs',
    'Backend/ProjectRpg.Backend/Configuration/BackendOptions.cs', 'Backend/ProjectRpg.Backend/appsettings.json',
    'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/Program.cs',
    'Backend/ProjectRpg.Backend.DungeonAbandonmentTests/DungeonDepartureTests.cs', 'Backend/test-dungeon-departure.ps1')
function Inputs { @($paths | ForEach-Object { [pscustomobject]@{Path=$_;Sha256=(Get-FileHash (Join-Path $projectRoot $_)).Hash} }) }
function Require([bool]$Condition,[string]$Message) { if(-not $Condition){throw $Message} }
$before=Inputs
$proof=[ordered]@{Passed=$false;Scope='Production departure HTTP Memory + focused Memory/private PG repository tests; no UE/Steam/Shipping';Inputs=$before}
$backend=$null; $passed=$false
$envNames=@('ASPNETCORE_ENVIRONMENT','DOTNET_ENVIRONMENT','PROJECT_RPG_BACKEND_ADMIN_TOKEN','POSTGRES_CONNECTION_STRING','PROJECT_RPG_ABANDON_TEST_POSTGRES')
$saved=@{}; foreach($name in $envNames){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
function Api([string]$Method,[string]$Route,$Body,[hashtable]$Headers,[int]$Expected=200) {
    $parameters=@{Method=$Method;Uri="$baseUrl/api/$Route";UseBasicParsing=$true;Headers=$Headers;TimeoutSec=5}
    if($null -ne $Body){$parameters.Body=$Body|ConvertTo-Json -Compress;$parameters.ContentType='application/json'}
    try{$response=Invoke-WebRequest @parameters;$status=[int]$response.StatusCode}
    catch{if($null -eq $_.Exception.Response){throw};$status=[int]$_.Exception.Response.StatusCode;$response=$null}
    Require ($status -eq $Expected) "HTTP $Method $Route expected $Expected received $status."
    if($null -ne $response -and $response.Content){$response.Content|ConvertFrom-Json}
}
try {
    & $dotnet build (Join-Path $PSScriptRoot 'ProjectRpg.Backend.DungeonAbandonmentTests/ProjectRpg.Backend.DungeonAbandonmentTests.csproj') -c Release --artifacts-path $build --nologo -v minimal *> (Join-Path $root 'build.log')
    Require ($LASTEXITCODE -eq 0) 'Departure test build failed.'
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_ABANDON_TEST_POSTGRES',$PostgresConnectionString,'Process')
    $testDll=Join-Path $build 'bin/ProjectRpg.Backend.DungeonAbandonmentTests/release/ProjectRpg.Backend.DungeonAbandonmentTests.dll'
    $testArgs=@($testDll,'--departure-only');if($RequirePostgres){$testArgs+='--require-postgres'}
    & $dotnet @testArgs *> (Join-Path $root 'repository.log')
    Require ($LASTEXITCODE -eq 0) 'Departure repository tests failed.'
    $proof.PostgresRequested= -not [string]::IsNullOrWhiteSpace($PostgresConnectionString)
    $backendDll=Join-Path $build 'bin/ProjectRpg.Backend/release/ProjectRpg.Backend.dll'
    $proof.BackendSha256=(Get-FileHash $backendDll).Hash
    $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
    $listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop();$baseUrl="http://127.0.0.1:$port"
    $adminToken=[Guid]::NewGuid().ToString('N')+[Guid]::NewGuid().ToString('N');$admin=@{Authorization="Bearer $adminToken"}
    [Environment]::SetEnvironmentVariable('ASPNETCORE_ENVIRONMENT','Development','Process')
    [Environment]::SetEnvironmentVariable('DOTNET_ENVIRONMENT','Development','Process')
    [Environment]::SetEnvironmentVariable('PROJECT_RPG_BACKEND_ADMIN_TOKEN',$adminToken,'Process')
    [Environment]::SetEnvironmentVariable('POSTGRES_CONNECTION_STRING',$null,'Process')
    $arguments=@(('"'+$backendDll+'"'),'--Storage:Provider=Memory','--Steam:AllowDevelopmentAuthentication=true',
        '--DungeonSessions:MinimumDeparturePartySize=2',"--Kestrel:Endpoints:Http:Url=$baseUrl")
    $backend=Start-Process -FilePath $dotnet -ArgumentList $arguments -WorkingDirectory (Split-Path $backendDll -Parent) -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $root 'backend.stdout.log') -RedirectStandardError (Join-Path $root 'backend.stderr.log')
    $ready=$false
    for($i=0;$i -lt 60;$i++){
        if($backend.HasExited){throw 'Backend exited before readiness.'}
        try{$null=Invoke-RestMethod "$baseUrl/health/ready" -TimeoutSec 1;$ready=$true;break}catch{Start-Sleep -Milliseconds 250}
    }
    Require $ready 'Backend readiness timeout.'
    $players=@(for($i=0;$i -lt 3;$i++){
        $steam=([uint64]76561198000777001+[uint64]$i).ToString()
        $login=Api Post 'auth/steam-ticket' @{ticket="dev:$steam"} @{}
        $headers=@{Authorization="Bearer $($login.accessToken)"}
        $character=Api Post 'characters' @{name="DepartCase$i"} $headers 201
        [pscustomobject]@{Character=$character.characterId;Headers=$headers}
    })
    $a=$players[0];$b=$players[1];$c=$players[2]
    $session=Api Post 'dungeon-sessions' @{characterId=$a.Character;dungeonId='Dungeon.RaidSwordMan';difficulty='Normal'} $a.Headers 201
    $id=$session.dungeonSessionId
    $command=@{characterId=$a.Character;requestId=[Guid]::NewGuid().ToString('D');expectedLobbyRevision=$session.lobbyRevision}
    $null=Api Post "dungeon-sessions/$id/departure" $command $admin 403
    $null=Api Post "dungeon-sessions/$id/departure" $command $a.Headers 409 # minimum 2
    $assignment=@{serverId='departure-server';serverAddress='127.0.0.1:7891'}
    $null=Api Post "dungeon-sessions/$id/activate" $assignment $admin 409
    $null=Api Post 'dungeon-sessions/claim' $assignment $admin 204
    $joined=Api Post "dungeon-sessions/$id/members" @{characterId=$b.Character} $b.Headers
    $null=Api Post "dungeon-sessions/$id/departure" $command $b.Headers 403 # forged leader
    $null=Api Post "dungeon-sessions/$id/departure" $command $a.Headers 409 # stale roster
    $command.expectedLobbyRevision=$joined.lobbyRevision
    $queued=Api Post "dungeon-sessions/$id/departure" $command $a.Headers
    $replay=Api Post "dungeon-sessions/$id/departure" $command $a.Headers
    Require ($queued.lobbyRevision -eq $replay.lobbyRevision -and $queued.expiresAt -ceq $joined.expiresAt) 'HTTP request replay mutated metadata/lease.'
    $null=Api Post "dungeon-sessions/$id/members" @{characterId=$c.Character} $c.Headers 409
    $cancelled=Api Post "dungeon-sessions/$id/departure/cancel" $command $a.Headers
    $cancelReplay=Api Post "dungeon-sessions/$id/departure/cancel" $command $a.Headers
    Require ($cancelled.lobbyRevision -eq $cancelReplay.lobbyRevision -and -not $cancelled.departureRequestedAt) 'HTTP cancel replay changed lobby.'
    $null=Api Post "dungeon-sessions/$id/departure" $command $a.Headers 409
    $next=@{characterId=$a.Character;requestId=[Guid]::NewGuid().ToString('D');expectedLobbyRevision=$cancelled.lobbyRevision}
    $null=Api Post "dungeon-sessions/$id/departure" $next $a.Headers
    $null=Api Post "dungeon-sessions/$id/departure/cancel" $command $a.Headers 409
    $claimed=Api Post 'dungeon-sessions/claim' $assignment $admin
    Require ($claimed.dungeonSessionId -ceq $id -and $claimed.state -ceq 'Loading' -and $claimed.serverMapPackage -ceq '/Game/Maps/bossmap') 'Strict claim did not assign the queued lobby.'
    $server=@{Authorization="Bearer $($claimed.gameServerAccessToken)"}
    $null=Api Post "dungeon-sessions/$id/departure" $next $server 403
    $null=Api Post "dungeon-sessions/$id/departure/cancel" $next $a.Headers 409
    $afterClaim=Api Post "dungeon-sessions/$id/departure" $next $a.Headers
    Require ($afterClaim.state -ceq 'Loading' -and $afterClaim.members.Count -eq 2) 'Lost-response replay rolled back claimed party.'
    $proof.HttpVerified=$true;$proof.SessionId=$id
    $proof.InputsUnchanged=(($before|ConvertTo-Json -Compress) -ceq ((Inputs)|ConvertTo-Json -Compress))
    Require $proof.InputsUnchanged 'Departure inputs changed during verification.'
    $passed=$true
}
finally {
    if($null -ne $backend -and -not $backend.HasExited){Stop-Process -Id $backend.Id -Force;$backend.WaitForExit()}
    foreach($name in $envNames){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
    $proof.Passed=$passed
    $proof.ClosedLogs=@(Get-ChildItem -LiteralPath $root -Filter '*.log'|ForEach-Object{[pscustomobject]@{Path=$_.Name;Sha256=(Get-FileHash $_.FullName).Hash}})
    $proof|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'Verification.proof.json') -Encoding UTF8
    Write-Output "Dungeon departure: Passed=$passed Root=$root"
}
