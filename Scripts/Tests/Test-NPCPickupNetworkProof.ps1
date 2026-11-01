$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\Network\NPCPickupNetworkProof.ps1')
$zero='00000000-0000-0000-0000-000000000000'
$owner='11111111-1111-1111-1111-111111111111'
$loser='22222222-2222-2222-2222-222222222222'
$item='33333333-3333-3333-3333-333333333333'
$request='44444444-4444-4444-4444-444444444444'
$reject1='55555555-5555-5555-5555-555555555555'
$reject2='66666666-6666-6666-6666-666666666666'
$reject3='77777777-7777-7777-7777-777777777777'
function New-Fixture {
    [pscustomobject]@{server=@(
        'RPG_NPC_PICKUP SERVER_DEATH NPC=38 Deaths=1 Authenticated=1 NativeSource=1',
        'RPG_NPC_PICKUP SERVER_DROP NPC=38 Pickup=62 Spawned=1 Activated=1 Ended=1 ClaimConsumed=1 TransientRow=1',
        "RPG_NPC_PICKUP SERVER_COMMIT Pickup=62 Item=$item Definition=GameItem.Equipment.Weapon.GreatSword Quantity=1 Revision=1 Retired=1 Owner=$owner Party=2 TotalItems=1"
    );clients=@(
        [pscustomobject]@{characterId=$owner;rows=@(
            "RPG_NPC_PICKUP CLIENT_READY Pickup=62 Character=$owner Available=1 Empty=1 InRange=1",
            "RPG_NPC_PICKUP CLIENT_SEND Pickup=62 Requests=2 ExistingRPC=1 Character=$owner",
            "RPG_NPC_PICKUP CLIENT_PARTY_COMPLETE Pickup=62 Character=$owner Success=1 Rejected=1 Request=$request Reject1=$reject1 Reject2=$zero Item=$item Quantity=1 Revision=1 Retired=1"
        )},
        [pscustomobject]@{characterId=$loser;rows=@(
            "RPG_NPC_PICKUP CLIENT_READY Pickup=62 Character=$loser Available=1 Empty=1 InRange=1",
            "RPG_NPC_PICKUP CLIENT_SEND Pickup=62 Requests=2 ExistingRPC=1 Character=$loser",
            "RPG_NPC_PICKUP CLIENT_PARTY_COMPLETE Pickup=62 Character=$loser Success=0 Rejected=2 Request=$zero Reject1=$reject2 Reject2=$reject3 Item=$zero Quantity=0 Revision=0 Retired=1"
        )}
    )}
}
$count=0
$fixture=New-Fixture
$proof=Get-NPCPickupPartyObservations $fixture.server $fixture.clients
if ($proof.winnerCharacterId -ne $owner -or $proof.itemId -ne $item -or $proof.rejectedRequestIds.Count -ne 3) { throw 'Valid 2-client fixture rejected/misread.' }
$count++
# Arrival/client order must not select the winner.
$proof=Get-NPCPickupPartyObservations $fixture.server @($fixture.clients[1],$fixture.clients[0])
if ($proof.winnerCharacterId -ne $owner) { throw 'Parser assumed client ordering.' }
$count++
function Assert-Rejected {
    param([string]$Name,[scriptblock]$Mutation)
    $fixture=New-Fixture
    & $Mutation $fixture
    $threw=$false
    try { $null=Get-NPCPickupPartyObservations $fixture.server $fixture.clients } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid proof: $Name" }
    $script:count++
}
Assert-Rejected 'borrowed actor' {param($f) $f.clients[1].rows[0]=$f.clients[1].rows[0].Replace('Pickup=62','Pickup=63')}
Assert-Rejected 'borrowed NPC' {param($f) $f.server[0]=$f.server[0].Replace('NPC=38','NPC=39')}
Assert-Rejected 'duplicate character' {param($f) $f.clients[1].characterId=$owner}
Assert-Rejected 'borrowed command' {param($f) $f.clients[1].rows[2]=$f.clients[1].rows[2].Replace($reject2,$request)}
Assert-Rejected 'duplicate rejections' {param($f) $f.clients[1].rows[2]=$f.clients[1].rows[2].Replace($reject3,$reject2)}
Assert-Rejected 'missing readiness' {param($f) $f.clients[1].rows=@($f.clients[1].rows | Select-Object -Skip 1)}
Assert-Rejected 'loser projection leak' {param($f) $f.clients[1].rows[2]=$f.clients[1].rows[2].Replace('Quantity=0','Quantity=1')}
Assert-Rejected 'wrong owner' {param($f) $f.server[2]=$f.server[2].Replace("Owner=$owner","Owner=$loser")}
Assert-Rejected 'wrong item' {param($f) $f.clients[0].rows[2]=$f.clients[0].rows[2].Replace("Item=$item","Item=$reject3")}
Assert-Rejected 'party count' {param($f) $f.server[2]=$f.server[2].Replace('Party=2','Party=4')}
Assert-Rejected 'multiple winners' {param($f) $f.clients[1].rows[2]=$f.clients[1].rows[2].Replace('Success=0','Success=1')}
Assert-Rejected 'extra line' {param($f) $f.server+=@($f.server[2])}
function New-AckFixture {
    $f=New-Fixture
    $f.server=@($f.server[0],$f.server[1], 'RPG_NPC_PICKUP SERVER_UNCERTAIN Pickup=62 Pending=1 Available=0 Retired=0 TotalItems=0 GatewayAttempts=4', $f.server[2])
    $f.clients[0].rows=@($f.clients[0].rows[0],$f.clients[0].rows[1],
        "RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=62 Character=$owner Request=$reject1 Retired=0 TotalItems=0",
        "RPG_NPC_PICKUP CLIENT_RECOVERED Pickup=62 Request=$request Code=AlreadyApplied", $f.clients[0].rows[2])
    $f.clients[1].rows=@($f.clients[1].rows[0],$f.clients[1].rows[1],
        "RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=62 Character=$loser Request=$reject2 Retired=0 TotalItems=0",
        "RPG_NPC_PICKUP CLIENT_REJECTED_PENDING Pickup=62 Character=$loser Request=$reject3 Retired=0 TotalItems=0", $f.clients[1].rows[2])
    return $f
}
$ack=New-AckFixture
$proof=Get-NPCPickupPartyObservations $ack.server $ack.clients -UnknownAck
if ($proof.requestId -ne $request -or $proof.rejectedRequestIds.Count -ne 3) { throw 'Valid uncertainty/recovery proof misread.' }
$count++
function Assert-AckRejected {
    param([string]$Name,[scriptblock]$Mutation)
    $f=New-AckFixture
    & $Mutation $f
    $threw=$false
    try { $null=Get-NPCPickupPartyObservations $f.server $f.clients -UnknownAck } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid recovery proof: $Name" }
    $script:count++
}
Assert-AckRejected 'borrowed pending pickup' {param($f) $f.server[2]=$f.server[2].Replace('Pickup=62','Pickup=63')}
Assert-AckRejected 'premature projection' {param($f) $f.server[2]=$f.server[2].Replace('TotalItems=0','TotalItems=1')}
Assert-AckRejected 'invalid gateway boundary' {param($f) $f.server[2]=$f.server[2].Replace('GatewayAttempts=4','GatewayAttempts=0')}
Assert-AckRejected 'duplicate pending rejection' {param($f) $f.clients[1].rows[3]=$f.clients[1].rows[2]}
Assert-AckRejected 'borrowed recovered request' {param($f) $f.clients[0].rows[3]=$f.clients[0].rows[3].Replace($request,$reject2)}
Assert-AckRejected 'fresh grant rather than recovery' {param($f) $f.clients[0].rows[3]=$f.clients[0].rows[3].Replace('AlreadyApplied','Succeeded')}
Assert-AckRejected 'missing recovery' {param($f) $f.clients[0].rows=@($f.clients[0].rows[0],$f.clients[0].rows[1],$f.clients[0].rows[2],$f.clients[0].rows[4])}
function New-RetryState {
    [pscustomobject]@{phase='blocked';failure=$null;droppedAcknowledgements=1;
        committed=[pscustomobject]@{requestId=$request;bodySHA256=('a'*64);elapsedMilliseconds=900;observedAtUtc='2026-10-12T00:00:00Z'};
        blockedRetries=@(foreach ($elapsed in @(1000,1200,1400,3400)) {
            [pscustomobject]@{requestId=$request;bodySHA256=('a'*64);elapsedMilliseconds=$elapsed;observedAtUtc='2026-10-12T00:00:00Z'}
        })}
}
$retry=New-RetryState
if ((Get-NPCPickupNativeRetryGap $retry) -ne 2.0) { throw 'Valid monotonic timer boundary misread.' }
$count++
foreach ($utc in @('2000-01-01T00:00:00Z','2099-01-01T00:00:00Z')) {
    $retry=New-RetryState
    $retry.blockedRetries[3].observedAtUtc=$utc
    if ((Get-NPCPickupNativeRetryGap $retry) -ne 2.0) { throw 'UTC clock change affected monotonic proof.' }
    $count++
}
function Assert-RetryRejected {
    param([string]$Name,[scriptblock]$Mutation)
    $f=New-RetryState
    & $Mutation $f
    $threw=$false
    try { $null=Get-NPCPickupNativeRetryGap $f } catch { $threw=$true }
    if (-not $threw) { throw "Accepted invalid monotonic proof: $Name" }
    $script:count++
}
Assert-RetryRejected 'short actual gap despite future UTC' {param($f) $f.blockedRetries[3].elapsedMilliseconds=1500; $f.blockedRetries[3].observedAtUtc='2099-01-01T00:00:00Z'}
Assert-RetryRejected 'missing monotonic timestamp' {param($f) $f.blockedRetries[3].elapsedMilliseconds=$null}
Assert-RetryRejected 'non-finite monotonic timestamp' {param($f) $f.blockedRetries[3].elapsedMilliseconds=[double]::PositiveInfinity}
Assert-RetryRejected 'decreasing monotonic clock' {param($f) $f.blockedRetries[1].elapsedMilliseconds=990}
Assert-RetryRejected 'different retry body' {param($f) $f.blockedRetries[1].bodySHA256=('b'*64)}
Assert-RetryRejected 'different retry ID' {param($f) $f.blockedRetries[1].requestId=$reject2}
$ack=New-AckFixture
$ack.server[2]=$ack.server[2].Replace('GatewayAttempts=4','GatewayAttempts=2')
$proof=Get-NPCPickupPartyObservations $ack.server $ack.clients -UnknownAck
if ($proof.gatewayAttempts -ne 2) { throw 'Effective native attempt limit was ignored.' }
$count++
$retry=New-RetryState
$retry.blockedRetries[0].elapsedMilliseconds=1000
$retry.blockedRetries[1].elapsedMilliseconds=3000
$retry.blockedRetries[2].elapsedMilliseconds=3100
$retry.blockedRetries[3].elapsedMilliseconds=7100
if ((Get-NPCPickupNativeRetryGap $retry -GatewayAttempts 2) -ne 2.0) { throw 'Two-attempt native timer boundary misread.' }
$count++
$retry=New-RetryState
$retry.blockedRetries[0].elapsedMilliseconds=2900
$retry.blockedRetries[1].elapsedMilliseconds=3000
$retry.blockedRetries[2].elapsedMilliseconds=3100
$retry.blockedRetries[3].elapsedMilliseconds=7100
if ((Get-NPCPickupNativeRetryGap $retry -GatewayAttempts 1) -ne 2.0) { throw 'Single-attempt native timer boundary misread.' }
$count++
Write-Host "PASS: $count NPC pickup party/recovery/timer proof checks. Parser-only; no UE/HTTP/DB runtime claim."
