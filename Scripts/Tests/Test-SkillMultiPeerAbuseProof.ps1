[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Test-SkillTargetDataAbuseProof.ps1')
. (Join-Path $PSScriptRoot '..\Network\SkillMultiPeerAbuseProof.ps1')

function Scope-Golden([string] $Text, [int] $Peer) {
    $Text = $Text.Replace('Connected=1 Expected=1', 'Connected=2 Expected=2')
    if ($Peer -eq 257) {
        $Text = $Text.Replace('ASCNetId=800', 'ASCNetId=900').Replace('Handle=10', 'Handle=20')
        for ($i=0; $i -lt 4; ++$i) { $Text = $Text.Replace("NetId=$($i+100)", "NetId=$($i+200)") }
        $Text = $Text.Replace('_C_0', '_C_1')
    }
    return (($Text -split '\r?\n' | ForEach-Object {
        if ($_ -match '^RPG_SKILL_') { "RPG_SKILL_PEER Peer=$Peer $_" } else { $_ }
    }) -join "`n")
}
$serverParts = @{}
$clients = @()
foreach ($peer in @(256,257)) {
    $other = if ($peer -eq 256) {257} else {256}
    $id = if ($peer -eq 256) {800} else {900}
    $otherId = if ($peer -eq 256) {900} else {800}
    $otherHandle = if ($peer -eq 256) {20} else {10}
    $otherTarget = if ($peer -eq 256) {200} else {100}
    $scopedServer = Scope-Golden $s $peer
    $firstHP = $scopedServer.IndexOf("RPG_SKILL_PEER Peer=$peer RPG_SKILL_ABUSE HP Index=0")
    $verifiedLine = "RPG_SKILL_PEER Peer=$peer RPG_SKILL_ABUSE VERIFIED Cases=7 HealthUnchanged=1"
    $verifiedEnd = $scopedServer.IndexOf($verifiedLine) + $verifiedLine.Length
    $challenge = $scopedServer.Substring($firstHP, $verifiedEnd-$firstHP)
    for ($case=0; $case -lt 7; ++$case) {
        $cross = @("RPG_SKILL_MULTI PEER_ACTIVE Peer=$peer OtherPeer=$other Index=$case ASCNetId=$otherId Handle=$otherHandle Original=1 Active=1 Events=0")
        for ($target=0; $target -lt 4; ++$target) {
            $cross += "RPG_SKILL_MULTI CROSS_HP Peer=$peer OtherPeer=$other Index=$case Target=$target NetId=$($otherTarget+$target) Health=1000000.000000 Effects=0"
        }
        if ($case -eq 4) { $cross = @("RPG_SKILL_MULTI FORGED_TARGET Role=Server Peer=$peer OtherPeer=$other NetId=$otherTarget") + $cross }
        $marker = "RPG_SKILL_PEER Peer=$peer RPG_SKILL_ABUSE HP Index=$case Target=0"
        $challenge = $challenge.Replace($marker, ($cross -join "`n") + "`n" + $marker)
    }
    $serverParts[$peer] = @{ prep=$scopedServer.Substring(0,$firstHP); challenge=$challenge; tail=$scopedServer.Substring($verifiedEnd) }
    $scopedClient = Scope-Golden $c $peer
    $primedRow = [regex]::Match($scopedClient, "(?m)^RPG_SKILL_PEER Peer=$peer RPG_SKILL_ABUSE PRIMED[^\r\n]*").Value
    $scopedClient = $scopedClient.Replace($primedRow,'')
    $activeRow = "RPG_SKILL_PEER Peer=$peer RPG_SKILL_NETTEST CLIENT_ACTIVE Ability=Default__GA_Player_ChargeSlash_C"
    $scopedClient = $scopedClient.Replace($activeRow,
        $activeRow + "`nRPG_SKILL_PEER Peer=$peer RPG_SKILL_NETTEST CLIENT_INPUT_PRIMED Action=IA_IdentitySkill FixtureBinding=1`n" + $primedRow)
    $crossBaseline = @()
    $crossResults = @()
    for ($target=0; $target -lt 4; ++$target) {
        $crossBaseline += "RPG_SKILL_MULTI CROSS_BASELINE Peer=$peer OtherPeer=$other Index=$target NetId=$($otherTarget+$target) Health=1000000.000000"
        $after = if ($target -eq 0) {'999500.000000'} else {'1000000.000000'}
        $notify = if ($target -eq 0) {1} else {0}
        $crossResults += "RPG_SKILL_MULTI CROSS_RESULT Peer=$peer OtherPeer=$other Index=$target NetId=$($otherTarget+$target) Before=1000000.000000 After=$after Notify=$notify"
    }
    $crossBaseline += "RPG_SKILL_MULTI FIXTURE_READY Peer=$peer ASCNetId=$id Targets=8"
    $crossResults += "RPG_SKILL_MULTI CLIENT_COMPLETE Peer=$peer OtherPeer=$other Targets=8"
    $scopedClient = $scopedClient.Replace("RPG_SKILL_PEER Peer=$peer RPG_SKILL_NETTEST CLIENT_INPUT_STARTED", ($crossBaseline -join "`n") + "`nRPG_SKILL_PEER Peer=$peer RPG_SKILL_NETTEST CLIENT_INPUT_STARTED")
    $scopedClient = $scopedClient.Replace("RPG_SKILL_PEER Peer=$peer RPG_SKILL_ABUSE SENT Index=4", "RPG_SKILL_MULTI FORGED_TARGET Role=Client Peer=$peer OtherPeer=$other NetId=$otherTarget`nRPG_SKILL_PEER Peer=$peer RPG_SKILL_ABUSE SENT Index=4")
    $scopedClient = $scopedClient.Replace("RPG_SKILL_PEER Peer=$peer RPG_SKILL_ABUSE RELEASE_READY", "RPG_SKILL_MULTI ALL_VERIFIED Peer=$peer OtherPeer=$other Cases=14 HealthUnchanged=1`nRPG_SKILL_PEER Peer=$peer RPG_SKILL_ABUSE RELEASE_READY")
    $scopedClient = $scopedClient.Replace("RPG_SKILL_PEER Peer=$peer RPG_SKILL_DAMAGE HP_NOTIFY", "RPG_SKILL_MULTI CROSS_NOTIFY Peer=$peer OtherPeer=$other Index=0 NetId=$otherTarget Old=1000000.000000 New=999500.000000`nRPG_SKILL_PEER Peer=$peer RPG_SKILL_DAMAGE HP_NOTIFY")
    $scopedClient = $scopedClient.Replace("RPG_SKILL_PEER Peer=$peer RPG_SKILL_DAMAGE RESULT Role=Client Index=0", ($crossResults -join "`n") + "`nRPG_SKILL_PEER Peer=$peer RPG_SKILL_DAMAGE RESULT Role=Client Index=0")
    $clients += $scopedClient
}
$multiServer = @(
    'RPG_SKILL_MULTI READY Peer=256 ASCNetId=800 Targets=8',
    'RPG_SKILL_MULTI READY Peer=257 ASCNetId=900 Targets=8',
    'RPG_SKILL_MULTI INPUT_READY Peer=256 ASCNetId=800 Targets=8',
    'RPG_SKILL_MULTI INPUT_READY Peer=257 ASCNetId=900 Targets=8',
    $serverParts[256].prep, $serverParts[257].prep,
    $serverParts[256].challenge, $serverParts[257].challenge,
    $serverParts[256].tail, $serverParts[257].tail
) -join "`n"
$multiProof = Get-SkillMultiPeerAbuseProof $multiServer $clients 250 450 10
if ($multiProof.actualMaliciousRequests -ne 14 -or $multiProof.peers.Count -ne 2 -or
    -not $multiProof.multiPeerDamageIsolationVerified -or -not $multiProof.simultaneousAuthoredActivationsVerified -or
    -not $multiProof.bothTargetGroupsReplicatedOnBothClientsVerified -or $multiProof.shippingRuntimeVerified -or
    $multiProof.finalCapRejectionOverWireVerified -or $multiProof.productionNpcLifecycleVerified -or
    -not $multiProof.controlledRepressThroughAuthoredInputBindingFixture -or $multiProof.physicalSecondPressVerified) { throw 'Golden multi-peer scope misread.' }
$script:multiChecks = 1
$swappedProof = Get-SkillMultiPeerAbuseProof $multiServer @($clients[1], $clients[0]) 250 450 10
if (-not $swappedProof.multiPeerDamageIsolationVerified) { throw 'Client order must not substitute for actual peer identity.' }
++ $script:multiChecks
function Reject-Multi([string] $Name, [string] $Server=$multiServer, [string[]] $Clients=$clients) {
    $rejected = $false
    try { Get-SkillMultiPeerAbuseProof $Server $Clients 250 450 10 | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid multi-peer evidence accepted: $Name" }
    ++$script:multiChecks
}
foreach ($marker in @('READY','INPUT_READY','PEER_ACTIVE','CROSS_HP','FORGED_TARGET')) {
    Reject-Multi "missing server $marker" ($multiServer.Replace("RPG_SKILL_MULTI $marker", "UNKNOWN $marker"))
}
foreach ($marker in @('CROSS_BASELINE','FIXTURE_READY','FORGED_TARGET','ALL_VERIFIED','CROSS_NOTIFY','CROSS_RESULT','CLIENT_COMPLETE')) {
    Reject-Multi "missing client $marker" $multiServer @($clients[0].Replace("RPG_SKILL_MULTI $marker", "UNKNOWN $marker"), $clients[1])
}
Reject-Multi 'one client only' $multiServer @($clients[0])
Reject-Multi 'same client twice' $multiServer @($clients[0], $clients[0])
Reject-Multi 'single-client readiness' ($multiServer.Replace('Connected=2 Expected=2', 'Connected=1 Expected=1'))
Reject-Multi 'missing peer scope' ($multiServer.Replace('RPG_SKILL_PEER Peer=256 ', ''))
Reject-Multi 'missing controlled input priming' $multiServer @($clients[0].Replace('RPG_SKILL_NETTEST CLIENT_INPUT_PRIMED', 'UNKNOWN_PRIMED'), $clients[1])
Reject-Multi 'unmarked synthetic priming' $multiServer @($clients[0].Replace('FixtureBinding=1', 'FixtureBinding=0'), $clients[1])
Reject-Multi 'priming belongs to another connection' $multiServer @($clients[0].Replace('Peer=256 RPG_SKILL_NETTEST CLIENT_INPUT_PRIMED', 'Peer=257 RPG_SKILL_NETTEST CLIENT_INPUT_PRIMED'), $clients[1])
$earlyInputPrime = [regex]::Match($clients[0], '(?m)^RPG_SKILL_PEER Peer=256 RPG_SKILL_NETTEST CLIENT_INPUT_PRIMED[^\r\n]*').Value
Reject-Multi 'priming precedes the actual activation' $multiServer @($earlyInputPrime + "`n" + $clients[0].Replace($earlyInputPrime,''), $clients[1])
Reject-Multi 'unknown combat peer' ($multiServer.Replace('RPG_SKILL_PEER Peer=256 ', 'RPG_SKILL_PEER Peer=258 '))
Reject-Multi 'shared player ASC' ($multiServer.Replace('ASCNetId=900', 'ASCNetId=800'))
Reject-Multi 'shared target group' ($multiServer.Replace('NetId=200', 'NetId=100'))
Reject-Multi 'other skill cancelled' ($multiServer.Replace('Active=1 Events=0', 'Active=0 Events=0'))
Reject-Multi 'other skill emitted damage' ($multiServer.Replace('Active=1 Events=0', 'Active=1 Events=1'))
Reject-Multi 'other activation key borrowed' ($multiServer.Replace('Handle=20 Original=1 Active=1', 'Handle=20 Original=2 Active=1'))
Reject-Multi 'other ASC mismatched' ($multiServer.Replace('Index=0 ASCNetId=900', 'Index=0 ASCNetId=901'))
Reject-Multi 'other peer mislabeled' ($multiServer.Replace('OtherPeer=257', 'OtherPeer=256'))
Reject-Multi 'cross HP changed during malice' ($multiServer.Replace('Health=1000000.000000 Effects=0', 'Health=999500.000000 Effects=0'))
Reject-Multi 'cross effect during malice' ($multiServer.Replace('Health=1000000.000000 Effects=0', 'Health=1000000.000000 Effects=1'))
Reject-Multi 'wrong submitted foreign actor' ($multiServer.Replace('Role=Server Peer=256 OtherPeer=257 NetId=200', 'Role=Server Peer=256 OtherPeer=257 NetId=201'))
Reject-Multi 'malformed extra evidence' ($multiServer + "`nRPG_SKILL_MULTI UNKNOWN")
Reject-Multi 'duplicate cross HP' ($multiServer + "`nRPG_SKILL_MULTI CROSS_HP Peer=256 OtherPeer=257 Index=0 Target=0 NetId=200 Health=1000000.000000 Effects=0")
Reject-Multi 'cross client notification lost' $multiServer @($clients[0].Replace('After=999500.000000 Notify=1', 'After=999500.000000 Notify=0'), $clients[1])
Reject-Multi 'cross client HP wrong' $multiServer @($clients[0].Replace('CROSS_RESULT Peer=256 OtherPeer=257 Index=0 NetId=200 Before=1000000.000000 After=999500.000000', 'CROSS_RESULT Peer=256 OtherPeer=257 Index=0 NetId=200 Before=1000000.000000 After=999499.000000'), $clients[1])
Reject-Multi 'cross notify wrong target' $multiServer @($clients[0].Replace('CROSS_NOTIFY Peer=256 OtherPeer=257 Index=0 NetId=200', 'CROSS_NOTIFY Peer=256 OtherPeer=257 Index=0 NetId=100'), $clients[1])
Reject-Multi 'packet override' ($multiServer + "`nLogNet: PktLoss set to 0")
$earlyPrimed = [regex]::Match($multiServer, '(?m)^RPG_SKILL_PEER Peer=257 RPG_SKILL_ABUSE PRIMED[^\r\n]*').Value
Reject-Multi 'second source not primed before malice' ($multiServer.Replace($earlyPrimed,'') + "`n" + $earlyPrimed)
$earlyRecovery = [regex]::Match($multiServer, '(?m)^RPG_SKILL_PEER Peer=256 RPG_SKILL_ABUSE RECOVERY[^\r\n]*').Value
Reject-Multi 'release before both challenges' ($earlyRecovery + "`n" + $multiServer.Replace($earlyRecovery,''))
$misplacedActive = [regex]::Match($multiServer, '(?m)^RPG_SKILL_MULTI PEER_ACTIVE Peer=256 OtherPeer=257 Index=6[^\r\n]*').Value
$case0Start = 'RPG_SKILL_MULTI PEER_ACTIVE Peer=256 OtherPeer=257 Index=0'
Reject-Multi 'other state sampled before the challenged case' ($multiServer.Replace($misplacedActive,'').Replace($case0Start, $misplacedActive + "`n" + $case0Start))
$misplacedHP = [regex]::Match($multiServer, '(?m)^RPG_SKILL_MULTI CROSS_HP Peer=256 OtherPeer=257 Index=6 Target=0[^\r\n]*').Value
Reject-Multi 'other HP sampled before the challenged case' ($multiServer.Replace($misplacedHP,'').Replace($case0Start, $misplacedHP + "`n" + $case0Start))
$prematureVerified = [regex]::Match($clients[0], '(?m)^RPG_SKILL_MULTI ALL_VERIFIED[^\r\n]*').Value
Reject-Multi 'client acknowledges abuse before sending it' $multiServer @($prematureVerified + "`n" + $clients[0].Replace($prematureVerified,''), $clients[1])
$prematureReady = [regex]::Match($clients[0], '(?m)^RPG_SKILL_MULTI FIXTURE_READY[^\r\n]*').Value
Reject-Multi 'client acknowledges fixture before own baseline' $multiServer @($prematureReady + "`n" + $clients[0].Replace($prematureReady,''), $clients[1])
foreach ($extra in @(@{ClientCount=1},@{ClientCount=3},@{Runtime='Game'},@{VerifySkillProgression=$true},
    @{VerifySkillTripods=$true},@{VerifyTargetDataDamage=$true},@{VerifyMovementSecurity=$true})) {
    $options = @{VerifySkillMultiPeerAbuse=$true;ClientCount=2;SkipBuild=$true;ProjectFile='Z:\absent.uproject'}
    foreach($key in $extra.Keys) { $options[$key] = $extra[$key] }
    $rejected = $false
    try { & (Join-Path $PSScriptRoot '..\Network\Run-IrisMultiClientTest.ps1') @options | Out-Null }
    catch { if ($_.Exception.Message -notmatch 'Run authored skill damage separately') { throw }; $rejected = $true }
    if (-not $rejected) { throw 'Invalid multi-peer CLI accepted.' }
    ++$script:multiChecks
}
Write-Host "PASS: $script:multiChecks multi-peer authored abuse proof/CLI checks (synthetic evidence only)."
