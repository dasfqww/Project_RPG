$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\Network\NPCPickupServerRestart.ps1')
. (Join-Path $PSScriptRoot '..\Network\PreparedPickupE2EProof.ps1')
$owner='11111111-1111-1111-1111-111111111111'
$loser='22222222-2222-2222-2222-222222222222'
$request='33333333-3333-3333-3333-333333333333'
$item='44444444-4444-4444-4444-444444444444'
$r1='55555555-5555-5555-5555-555555555555'
$r2='66666666-6666-6666-6666-666666666666'
$r3='77777777-7777-7777-7777-777777777777'
$committed=[pscustomobject]@{requestId=$request;characterId=$owner;itemId=$item;bodySHA256=('a'*64);recordSHA256=('b'*64)}
$server=@('RPG_NPC_PICKUP SERVER_DEATH NPC=40 Deaths=1 Authenticated=1 NativeSource=1',
    'RPG_NPC_PICKUP SERVER_DROP NPC=40 Pickup=62 Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1',
    'RPG_NPC_PICKUP SERVER_UNCERTAIN Pickup=62 Pending=1 Available=0 Retired=0 TotalItems=0 GatewayAttempts=2')
$clients=@([pscustomobject]@{characterId=$owner;rows=@(
    "RPG_NPC_PICKUP CLIENT_READY Pickup=62 Character=$owner Available=1 Empty=1 InRange=1",
    "RPG_NPC_PICKUP CLIENT_SEND Pickup=62 Requests=2 ExistingRPC=1 Character=$owner",
    "RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=62 Character=$owner Request=$r1 Retired=0 TotalItems=0")},
    [pscustomobject]@{characterId=$loser;rows=@(
    "RPG_NPC_PICKUP CLIENT_READY Pickup=62 Character=$loser Available=1 Empty=1 InRange=1",
    "RPG_NPC_PICKUP CLIENT_SEND Pickup=62 Requests=2 ExistingRPC=1 Character=$loser",
    "RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=62 Character=$loser Request=$r2 Retired=0 TotalItems=0",
    "RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=62 Character=$loser Request=$r3 Retired=0 TotalItems=0")})
$newServer=@("RPG_NPC_PICKUP SERVER_RESTART_RECOVERED Owner=$owner Request=$request Item=$item Quantity=1 Revision=1 Party=2 TotalItems=1 NewPickup=0")
$newClients=@([pscustomobject]@{characterId=$owner;rows=@("RPG_NPC_PICKUP CLIENT_RESTART_RECOVERED Character=$owner Owner=$owner Request=$request Item=$item TotalItems=1 Quantity=1 Revision=1 NewPickup=0")},
    [pscustomobject]@{characterId=$loser;rows=@("RPG_NPC_PICKUP CLIENT_RESTART_RECOVERED Character=$loser Owner=$owner Request=$request Item=$item TotalItems=0 Quantity=0 Revision=0 NewPickup=0")})
$state=[pscustomobject]@{phase='restart-read-only';failure=$null;droppedAcknowledgements=1;delivered=$null;committed=$committed;
    restartRecovery=[pscustomobject]@{elapsedMilliseconds=1000;rejectedWrites=0;reads=@(
        [pscustomobject]@{kind='receipt';requestId=$request;recordSHA256=('b'*64);statusCode=200;elapsedMilliseconds=1100},
        [pscustomobject]@{kind='inventory';characterId=$owner;statusCode=200;elapsedMilliseconds=1200},
        [pscustomobject]@{kind='inventory';characterId=$loser;statusCode=200;elapsedMilliseconds=1300})}}
function Copy-Value($Value) { $Value | ConvertTo-Json -Depth 10 | ConvertFrom-Json }
$script:checks=0
function Reject([string]$Name,[scriptblock]$Action) {
    $rejected=$false
    try { & $Action } catch { $rejected=$true }
    if (-not $rejected) { throw "Expected rejection: $Name" }
    $script:checks++
}
$boundary=Get-NPCPickupRestartBoundary $server $clients $committed
if ($boundary.gatewayAttempts -ne 2 -or $boundary.rejectedRequestIds.Count -ne 3) { throw 'Positive boundary mismatch.' }
$script:checks++
Assert-NPCPickupRestartRecovery $newServer $newClients $committed $state
$script:checks++
Reject 'borrowed pickup' { $s=$server.Clone(); $s[2]=$s[2].Replace('Pickup=62','Pickup=63'); Get-NPCPickupRestartBoundary $s $clients $committed }
Reject 'early acknowledgement' { $c=Copy-Value $clients; $c[0].rows+=('RPG_NPC_PICKUP CLIENT_RECOVERED Pickup=62 Request='+$request+' Code=AlreadyApplied'); Get-NPCPickupRestartBoundary $server $c $committed }
Reject 'duplicate rejection' { $c=Copy-Value $clients; $c[1].rows[3]=$c[1].rows[3].Replace($r3,$r2); Get-NPCPickupRestartBoundary $server $c $committed }
Reject 'borrowed request' { $c=Copy-Value $clients; $c[0].rows[2]=$c[0].rows[2].Replace($r1,$request); Get-NPCPickupRestartBoundary $server $c $committed }
Reject 'duplicate claimant' { $c=Copy-Value $clients; $c[1].characterId=$owner; Get-NPCPickupRestartBoundary $server $c $committed }
Reject 'different owner' { Assert-NPCPickupRestartRecovery @($newServer[0].Replace("Owner=$owner","Owner=$loser")) $newClients $committed $state }
Reject 'other item' { Assert-NPCPickupRestartRecovery @($newServer[0].Replace($item,$r3)) $newClients $committed $state }
Reject 'duplicate client' { $c=Copy-Value $newClients; $c[1]=$c[0]; Assert-NPCPickupRestartRecovery $newServer $c $committed $state }
Reject 'loser item leak' { $c=Copy-Value $newClients; $c[1].rows[0]=$c[1].rows[0].Replace('TotalItems=0 Quantity=0 Revision=0','TotalItems=1 Quantity=1 Revision=1'); Assert-NPCPickupRestartRecovery $newServer $c $committed $state }
Reject 'write after restart' { $s=Copy-Value $state; $s.restartRecovery.rejectedWrites=1; Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
Reject 'no receipt GET' { $s=Copy-Value $state; $s.restartRecovery.reads=@($s.restartRecovery.reads | Where-Object kind -ne 'receipt'); Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
Reject 'changed full record' { $s=Copy-Value $state; $s.restartRecovery.reads[0].recordSHA256='c'*64; Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
Reject 'non-200 receipt' { $s=Copy-Value $state; $s.restartRecovery.reads[0].statusCode=403; Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
Reject 'missing other inventory' { $s=Copy-Value $state; $s.restartRecovery.reads=@($s.restartRecovery.reads | Where-Object characterId -ne $loser); Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
Reject 'old receipt read' { $s=Copy-Value $state; $s.restartRecovery.reads[0].elapsedMilliseconds=999; Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
Reject 'invalid monotonic time' { $s=Copy-Value $state; $s.restartRecovery.reads[0].elapsedMilliseconds=[double]::NaN; Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
Reject 'borrowed committed state' { $s=Copy-Value $state; $s.committed.itemId=$r2; Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
Reject 'live retry instead of restart read' { $s=Copy-Value $state; $s.delivered=[pscustomobject]@{requestId=$request}; Assert-NPCPickupRestartRecovery $newServer $newClients $committed $s }
$drop='88888888-8888-8888-8888-888888888888'; $session='99999999-9999-9999-9999-999999999999'
$empty='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
$preparedCommitted=[pscustomobject]@{requestId=$request;characterId=$owner;itemId=$item;bodySHA256=$empty;
    recordSHA256=('b'*64);commandSHA256=('c'*64);journalSHA256=('d'*64);upstreamStatus='Committed';elapsedMilliseconds=150}
$preparedState=Copy-Value $state
$preparedState.committed=$preparedCommitted
$preparedState | Add-Member prepared ([pscustomobject]@{fault='consume-held';rejectedWrites=0;reads=@();
    origin=[pscustomobject]@{sessionId=$session;serverId='test-server';dropId=$drop;requestId=$request;itemId=$item;characterId=$owner;
        commandSHA256=('c'*64);prepareBodySHA256=('e'*64);journalSHA256=('d'*64);journalSchemaVersion=2;journalVerifiedBeforePrepare=$true};
    preparations=@(
        [pscustomobject]@{status='Prepared';acknowledgement='delivered';consumed=$false;commandSHA256=('c'*64);bodySHA256=('e'*64);journalSHA256=('d'*64);requestElapsedMilliseconds=80;elapsedMilliseconds=100},
        [pscustomobject]@{status='AlreadyPrepared';acknowledgement='delivered';consumed=$true;commandSHA256=('c'*64);bodySHA256=('e'*64);journalSHA256=('d'*64);requestElapsedMilliseconds=2300;elapsedMilliseconds=2350});
    consumes=@([pscustomobject]@{status='Committed';acknowledgement='dropped';requestId=$request;itemId=$item;bodyBytes=0;bodySHA256=$empty;
        commandSHA256=('c'*64);recordSHA256=('b'*64);journalSHA256=('d'*64);elapsedMilliseconds=150})})
$preparedState | Add-Member blockedRetries @(200,250,2400,2450 | ForEach-Object {
    [pscustomobject]@{stage='consume';requestId=$request;bodySHA256=$empty;commandSHA256=('c'*64);journalSHA256=('d'*64);elapsedMilliseconds=$_}
})
$preparedState.restartRecovery=[pscustomobject]@{elapsedMilliseconds=10000;rejectedWrites=0;preparationCount=2;consumeCount=1;blockedRetryCount=4;reads=@(
    [pscustomobject]@{kind='intent';requestId=$request;dropId=$drop;commandSHA256=('c'*64);journalSHA256=('d'*64);consumed=$true;
        preparedAt='2026-10-16T00:00:00Z';consumedAt='2026-10-16T00:00:01Z';statusCode=200;acknowledgement='delivered';requestElapsedMilliseconds=11000;elapsedMilliseconds=11100},
    [pscustomobject]@{kind='receipt';requestId=$request;recordSHA256=('b'*64);statusCode=200;acknowledgement='delivered';requestElapsedMilliseconds=11200;elapsedMilliseconds=11300},
    [pscustomobject]@{kind='inventory';characterId=$owner;itemCount=1;recordSHA256=('b'*64);statusCode=200;acknowledgement='delivered';requestElapsedMilliseconds=11400;elapsedMilliseconds=11500},
    [pscustomobject]@{kind='inventory';characterId=$loser;itemCount=0;recordSHA256=$null;statusCode=200;acknowledgement='delivered';requestElapsedMilliseconds=11600;elapsedMilliseconds=11700})}
$preparedServer=$server.Clone(); $preparedServer[1]+=" DropId=$drop"
$null=Get-NPCPickupRestartBoundary $preparedServer $clients $preparedCommitted -PreparedDropId $drop; $script:checks++
Assert-NPCPickupRestartRecovery $newServer $newClients $preparedCommitted $preparedState -Prepared; $script:checks++
Reject 'foreign prepared boundary Drop' { Get-NPCPickupRestartBoundary $preparedServer $clients $preparedCommitted -PreparedDropId $r1 }
foreach ($case in @(
    @('missing v2 evidence',{param($s) $s.prepared=$null}),
    @('new prepare after restart',{param($s) $s.prepared.preparations+= $s.prepared.preparations[1]}),
    @('new consume after restart',{param($s) $s.prepared.consumes+= $s.prepared.consumes[0]}),
    @('blocked retry after restart',{param($s) $s.blockedRetries+= $s.blockedRetries[0]}),
    @('write was rejected during recovery',{param($s) $s.restartRecovery.rejectedWrites=1}),
    @('no original intent GET',{param($s) $s.restartRecovery.reads=@($s.restartRecovery.reads | Where-Object kind -ne 'intent')}),
    @('unconsumed reservation',{param($s) $s.restartRecovery.reads[0].consumed=$false}),
    @('foreign intent Drop ID',{param($s) $s.restartRecovery.reads[0].dropId=$r1}),
    @('changed full intent command',{param($s) $s.restartRecovery.reads[0].commandSHA256='f'*64}),
    @('changed original journal',{param($s) $s.restartRecovery.reads[0].journalSHA256='f'*64}),
    @('undelivered intent',{param($s) $s.restartRecovery.reads[0].acknowledgement='pending'}),
    @('intent returned 404',{param($s) $s.restartRecovery.reads[0].statusCode=404}),
    @('receipt request before intent response',{param($s) $s.restartRecovery.reads[1].requestElapsedMilliseconds=11050}),
    @('owner load before receipt response',{param($s) $s.restartRecovery.reads[2].requestElapsedMilliseconds=11250}),
    @('owner full inventory differs',{param($s) $s.restartRecovery.reads[2].recordSHA256='f'*64}),
    @('loser inventory leaked',{param($s) $s.restartRecovery.reads[3].itemCount=1}),
    @('missing loser inventory',{param($s) $s.restartRecovery.reads=@($s.restartRecovery.reads | Where-Object characterId -ne $loser)}),
    @('NaN GET request',{param($s) $s.restartRecovery.reads[0].requestElapsedMilliseconds=[double]::NaN}),
    @('borrowed original full receipt',{param($s) $s.committed.recordSHA256='f'*64}),
    @('changed re-prepare body',{param($s) $s.prepared.preparations[1].bodySHA256='f'*64})
)) {
    $change=$case[1]
    Reject $case[0] { $s=Copy-Value $preparedState; & $change $s; Assert-NPCPickupRestartRecovery $newServer $newClients $preparedCommitted $s -Prepared }
}
Write-Host "PASS: $checks restart boundary/recovery proof cases (offline observations only)."
