[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Network\TargetDataWireProof.ps1')
$names = @('Baseline', 'Duplicate', 'Reversed', 'WrongActivation', 'ForeignSpec', 'OversizedActors',
    'DuplicateCancel', 'Recovery', 'EndedReplay', 'Reactivate', 'OldActivationReplay', 'ForeignOwner')
$rx = @(1, 4, 2, 1, 1, 1, 0, 1, 1, 0, 2, 1)
$accepted = @(1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1)
$serverLines = @(foreach ($peer in 256, 257) {
    "RPG_TARGETDATA_WIRE IDENTIFIED Peer=$peer Root=TargetDataWirePeer_$peer NetId=$($peer * 10)"
    $foreign = if ($peer -eq 256) { 257 } else { 256 }
    "RPG_TARGETDATA_WIRE FOREIGN_CHALLENGE Peer=$peer ForeignPeer=$foreign Root=TargetDataWirePeer_$foreign NetId=$($foreign * 10) Handle=$foreign Original=42 Active=1"
    for ($i = 0; $i -lt 12; ++$i) {
        $crx = if ($i -eq 6) { 2 } else { 0 }
        $cancel = if ($i -eq 6) { 1 } else { 0 }
        "RPG_TARGETDATA_WIRE CASE Peer=$peer Index=$i Name=$($names[$i]) RX=$($rx[$i]) CancelRX=$crx Accepted=$($accepted[$i]) Cancelled=$cancel CacheSafe=1"
    }
    "RPG_TARGETDATA_WIRE SERVER_COMPLETE Peer=$peer Cases=12 RX=15 CancelRX=2 Accepted=5 Cancelled=1"
    "LogIrisRpc: Verbose: Rejected reliable RPC function ServerSetReplicatedTargetData due to target not being owned by the connection. NetRefHandle (Id=$($peer * 10)) : URPGTargetDataWireTestASC /Game/Maps/testmap.TargetDataWirePeer_$peer.WireASC"
})
$server = $serverLines -join "`n"
$clients = @(foreach ($peer in 256, 257) {
    $foreign = if ($peer -eq 256) { 257 } else { 256 }
    "RPG_TARGETDATA_WIRE FOREIGN_QUEUED Peer=$peer ForeignPeer=$foreign RootLocal=RPGTargetDataWireTestActor_$peer NetId=$($foreign * 10) Handle=$foreign Original=42`nRPG_TARGETDATA_WIRE CLIENT_COMPLETE Peer=$peer Cases=12"
})
$proof = Get-TargetDataWireProof -ServerText $server -ClientTexts $clients
if (-not $proof.irisReceivingOwnershipVerified -or -not $proof.foreignActiveTokenForgeryVerified -or
    $proof.schemaVersion -ne 3 -or $proof.ownershipRejections.Count -ne 2 -or
    $proof.actualCombatEffectsVerified -or $proof.wireRateFloodVerified -or $proof.shippingRuntimeVerified) {
    throw 'Valid wire evidence or scope flags were misread.'
}
$script:passed = 1
function Test-Rejected([string] $Name, [string] $Text, [string[]] $ClientLogs = $clients) {
    $rejected = $false
    try { Get-TargetDataWireProof -ServerText $Text -ClientTexts $ClientLogs | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid proof accepted: $Name" }
    ++$script:passed
}
Test-Rejected 'receiver never reached' ($server.Replace('Name=Duplicate RX=4', 'Name=Duplicate RX=0'))
Test-Rejected 'duplicate approved' ($server.Replace('Name=Duplicate RX=4 CancelRX=0 Accepted=0', 'Name=Duplicate RX=4 CancelRX=0 Accepted=1'))
Test-Rejected 'cache allocated' ($server.Replace('CacheSafe=1', 'CacheSafe=0'))
Test-Rejected 'missing server identity' ($server.Replace('IDENTIFIED Peer=257', 'UNKNOWN Peer=257'))
Test-Rejected 'duplicate identity' ($server.Replace('IDENTIFIED Peer=257', 'IDENTIFIED Peer=256'))
Test-Rejected 'malformed identity' ($server.Replace('Root=TargetDataWirePeer_257', 'Root=Invalid'))
Test-Rejected 'duplicate client' $server @($clients[0], $clients[0])
Test-Rejected 'missing client completion' $server @($clients[0], $clients[1].Replace('CLIENT_COMPLETE', 'INCOMPLETE'))
Test-Rejected 'no receiving-side rejection' ($server.Replace('due to target not being owned', 'LOCAL no owning connection'))
Test-Rejected 'wrong rejected RPC' ($server.Replace('RPC function ServerSetReplicatedTargetData', 'RPC function ServerCheckpoint'))
Test-Rejected 'wrong rejected root' ($server.Replace('testmap.TargetDataWirePeer_257.WireASC', 'testmap.OtherActor.WireASC'))
Test-Rejected 'self targeting' $server @($clients[0].Replace('ForeignPeer=257', 'ForeignPeer=256'), $clients[1])
Test-Rejected 'missing owned positive control' ($server.Replace('Name=ForeignOwner RX=1', 'Name=ForeignOwner RX=0'))
Test-Rejected 'missing case' ($server.Replace('Index=10 Name=', 'Index=99 Name='))
Test-Rejected 'duplicate case' ($server + "`n" + $serverLines[2])
Test-Rejected 'incorrect totals' ($server.Replace('RX=15 CancelRX=2 Accepted=5', 'RX=16 CancelRX=2 Accepted=5'))
Test-Rejected 'runtime failure' ($server + "`nRPG_TARGETDATA_WIRE FAILED")
Test-Rejected 'runtime ensure' ($server + "`nEnsure condition failed: test")
Test-Rejected 'one client only' $server @($clients[0])
Test-Rejected 'zeroed foreign prediction key' $server @($clients[0].Replace('Original=42', 'Original=0'), $clients[1])
Test-Rejected 'wrong foreign prediction key' $server @($clients[0].Replace('Original=42', 'Original=43'), $clients[1])
Test-Rejected 'foreign ability no longer active' ($server.Replace('Active=1', 'Active=0'))
Test-Rejected 'ungranted foreign handle' $server @($clients[0].Replace('Handle=257', 'Handle=999'), $clients[1])
Test-Rejected 'duplicate network identity' ($server.Replace('NetId=2570', 'NetId=2560'))
Test-Rejected 'wrong client target network identity' $server @($clients[0].Replace('NetId=2570', 'NetId=9990'), $clients[1])
Test-Rejected 'zero network identity' ($server.Replace('NetId=2560', 'NetId=0'))
Test-Rejected 'wrong engine rejected network identity' ($server.Replace('NetRefHandle (Id=2570)', 'NetRefHandle (Id=9990)'))
Write-Host "PASS: $script:passed TargetData wire proof validator checks."
