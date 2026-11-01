$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'NPCDeathNetworkProof.ps1')
$server=@(
    'SERVER_READY Actor=10 ASC=11 Health=70.000000',
    'SERVER_DEATH Actor=10 ASC=11 Health=0 TagCount=1 Activations=1 Montage=AM_Guardian_Death CorpseRejected=3',
    'SERVER_REPLACEMENT OldActor=10 OldASC=11 Actor=20 ASC=21 Health=70.000000 TagCount=0 StaleRejected=1 Ended=1 Cancelled=0',
    'SERVER_COMPLETE Actor=20 ASC=21 Before=70.000000 After=60.000000 Damage=10.000000 ForcedReplacement=1 NativeSource=1 PausedAI=1'
) | ForEach-Object { 'LogTemp: Display: RPG_NPC_NET '+$_ }
$client=@(
    'CLIENT_READY Peer=256 Actor=10 ASC=11 Health=70.000000',
    'CLIENT_DEATH Peer=256 Actor=10 ASC=11 Health=0 TagCount=1 HPNotified=1 Montage=AM_Guardian_Death',
    'CLIENT_REPLACEMENT Peer=256 OldActor=10 OldASC=11 Actor=20 ASC=21 Health=70.000000 TagCount=0 OldRemoved=1',
    'CLIENT_COMPLETE Peer=256 Actor=20 ASC=21 Before=70.000000 After=60.000000 Damage=10.000000'
) | ForEach-Object { 'LogTemp: Display: RPG_NPC_NET '+$_ }
$serverText=$server -join "`n"; $clientText=$client -join "`n"; $second=$clientText.Replace('Peer=256','Peer=257')
$proof=Get-NPCDeathNetworkProof -ServerText $serverText -ClientTexts @($clientText,$second)
if (-not $proof.deathTagReplicationVerified -or $proof.shippingRuntimeVerified -or $proof.playerAuthoredInputVerified -or
    $proof.inPlaceResetVerified -or $proof.lootSettlementVerified -or -not $proof.nativeSourceFixture) {throw 'Proof scope mismatch'}
$checks=1
function Reject([string] $Name,[string] $Server,[string[]] $Clients) {
    $rejected=$false
    try {$null=Get-NPCDeathNetworkProof -ServerText $Server -ClientTexts $Clients} catch {$rejected=$true}
    if (-not $rejected) {throw "Unproven evidence accepted: $Name"}
    ++$script:checks
}
Reject 'Same peer twice' $serverText @($clientText,$clientText)
Reject 'One client' $serverText @($clientText)
foreach ($pair in @(@('Actor=20','Actor=10'),@('ASC=21','ASC=11'),@('TagCount=1','TagCount=0'),
    @('HPNotified=1','HPNotified=0'),@('Montage=AM_Guardian_Death','Montage=Other'),@('OldRemoved=1','OldRemoved=0'),
    @('After=60.000000','After=59.000000'),@('OldASC=11','OldASC=99'),@('Peer=256','Peer=0'))) {
    Reject $pair[0] $serverText @($clientText.Replace($pair[0],$pair[1]),$second)
}
foreach ($pair in @(@('CorpseRejected=3','CorpseRejected=0'),@('StaleRejected=1','StaleRejected=0'),
    @('Cancelled=0','Cancelled=1'),@('ForcedReplacement=1','ForcedReplacement=0'),@('After=60.000000','After=69.000000'))) {
    Reject $pair[0] ($serverText.Replace($pair[0],$pair[1])) @($clientText,$second)
}
for ($i=0;$i -lt 4;++$i) {
    Reject "Missing client stage $i" $serverText @(((@($client | Where-Object {$_ -ne $client[$i]})) -join "`n"),$second)
    Reject "Duplicate client stage $i" $serverText @(($clientText+"`n"+$client[$i]),$second)
    Reject "Missing server stage $i" ((@($server | Where-Object {$_ -ne $server[$i]})) -join "`n") @($clientText,$second)
}
Reject 'Client failure' $serverText @(($clientText+"`nRPG_NPC_NET FAILED Reason=Test"),$second)
Reject 'Reprinted client rows' $serverText @($clientText.Replace('LogTemp: Display:','LogAutomationController: LogTemp: Display:'),$second)
Reject 'Reordered client stages' $serverText @((@($client[1],$client[0],$client[2],$client[3]) -join "`n"),$second)
Write-Host "PASS: $checks synthetic NPC network evidence checks; not runtime evidence."
