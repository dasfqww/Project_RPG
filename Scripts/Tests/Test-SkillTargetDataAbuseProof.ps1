[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Test-SkillDamageProof.ps1')
. (Join-Path $PSScriptRoot '..\Network\SkillTargetDataAbuseProof.ps1')
$names = @('ReplayInitial','AlteredReplay','WrongActivation','OversizedActors','ActorForgery','OutOfRange','UnexpectedTag')
$messages = @(2,2,4,5,6,7,8)
$serverAbuse = @(
    'RPG_SKILL_ABUSE_CONSUMER Handle=10 Original=1 KnownTag=1 Decoded=1 Validated=1 Waiting=1',
    'RPG_SKILL_ABUSE INITIAL Role=Server ASCNetId=800 Handle=10 Original=1 Message=2 Entries=1 Valid=1',
    'RPG_SKILL_ABUSE_CONSUMER Handle=10 Original=1 KnownTag=1 Decoded=1 Validated=1 Waiting=0',
    'RPG_SKILL_ABUSE PRIMED Role=Server ASCNetId=800 Handle=10 Original=1 Message=3 Entries=1 Valid=1'
)
$clientAbuse = @()
for ($i=0; $i -lt 7; ++$i) {
    $orig = if ($i -eq 2) { 32767 } else { 1 }
    $entries = if ($i -eq 3) { 3 } else { 1 }
    $admitted = if ($i -ge 4) { 1 } else { 0 }
    if($admitted) {
        $known=if($i -eq 6) {0} else {1}
        $decoded=if($i -eq 4) {0} else {1}
        $serverAbuse += "RPG_SKILL_ABUSE_CONSUMER Handle=10 Original=1 KnownTag=$known Decoded=$decoded Validated=0 Waiting=0"
    }
    for ($target=0; $target -lt 4; ++$target) {
        $serverAbuse += "RPG_SKILL_ABUSE HP Index=$i Target=$target NetId=$($target + 100) Health=1000000.000000 Effects=0"
    }
    $common = "Index=$i Name=$($names[$i]) ASCNetId=800 Handle=10 Original=$orig Message=$($messages[$i]) Entries=$entries"
    $serverAbuse += "RPG_SKILL_ABUSE RECEIVED $common Admitted=$admitted TargetValid=$(1-$admitted) Active=1 Events=0"
    $clientAbuse += "RPG_SKILL_ABUSE SENT $common"
}
$serverAbuse += 'RPG_SKILL_ABUSE VERIFIED Cases=7 HealthUnchanged=1',
    'RPG_SKILL_ABUSE_CONSUMER Handle=10 Original=1 KnownTag=1 Decoded=1 Validated=1 Waiting=0',
    'RPG_SKILL_ABUSE RECOVERY ASCNetId=800 Handle=10 Original=1 Message=9 Valid=1 Events=0 Effects=0'
$clientAbuse += 'RPG_SKILL_ABUSE RELEASE_READY ASCNetId=800 Original=1 Cases=7 HealthUnchanged=1'
$s = $server.Replace('RPG_SKILL_NETTEST RELEASE_SECTION Role=Server',
    ($serverAbuse -join "`n") + "`nRPG_SKILL_NETTEST RELEASE_SECTION Role=Server")
$s = $s.Replace('RPG_SKILL_NETTEST SERVER_COMPLETE',
    "RPG_SKILL_ABUSE SERVER_COMPLETE Cases=7 Recovery=1 Targets=4`nRPG_SKILL_NETTEST SERVER_COMPLETE")
$c = $client.Replace('RPG_SKILL_NETTEST CLIENT_ACTIVE',
    "RPG_SKILL_ABUSE INITIAL Role=Client ASCNetId=800 Handle=10 Original=1 Message=2 Entries=1 Valid=1`nRPG_SKILL_ABUSE PRIMED Role=Client ASCNetId=800 Handle=10 Original=1 Message=3 Entries=1 Valid=1`nRPG_SKILL_NETTEST CLIENT_ACTIVE")
$c = $c.Replace('RPG_SKILL_NETTEST CLIENT_INPUT_STOPPED',
    ($clientAbuse -join "`n") + "`nRPG_SKILL_NETTEST CLIENT_INPUT_STOPPED")
$c = $c.Replace('RPG_SKILL_NETTEST CLIENT_COMPLETE',
    "RPG_SKILL_ABUSE CLIENT_COMPLETE Cases=7 Recovery=1 Targets=4`nRPG_SKILL_NETTEST CLIENT_COMPLETE")
$p = Get-SkillTargetDataAbuseProof $s $c 250 450 10
if (-not $p.authoredTargetDataConsumerVerified -or -not $p.actualHPUnchangedDuringMaliciousRequestsVerified -or
    -not $p.normalInputReleaseRecoveredDamageVerified -or $p.multiPeerDamageIsolationVerified -or
    $p.finalCapRejectionOverWireVerified -or $p.shippingRuntimeVerified -or $p.expectedNormalHealthReduction -ne 500) {
    throw 'Golden authored abuse proof/scope misread.'
}
$script:abuseChecks=1
function Reject-Abuse([string] $Name, [string] $Server=$s, [string] $Client=$c) {
    $rejected=$false
    try { Get-SkillTargetDataAbuseProof $Server $Client 250 450 10 | Out-Null } catch { $rejected=$true }
    if (-not $rejected) { throw "Invalid authored abuse evidence accepted: $Name" }
    ++$script:abuseChecks
}
foreach ($marker in 'INITIAL','PRIMED','RECEIVED','HP','VERIFIED','RECOVERY','SERVER_COMPLETE') {
    Reject-Abuse "server missing $marker" ($s.Replace("RPG_SKILL_ABUSE $marker", "UNKNOWN $marker"))
}
foreach ($marker in 'INITIAL','PRIMED','SENT','RELEASE_READY','CLIENT_COMPLETE') {
    Reject-Abuse "client missing $marker" $s ($c.Replace("RPG_SKILL_ABUSE $marker", "UNKNOWN $marker"))
}
Reject-Abuse 'wrong ASC on client' $s ($c.Replace('ASCNetId=800', 'ASCNetId=801'))
Reject-Abuse 'zero ASC' ($s.Replace('ASCNetId=800', 'ASCNetId=0')) ($c.Replace('ASCNetId=800', 'ASCNetId=0'))
Reject-Abuse 'borrow target identity' ($s.Replace('ASCNetId=800', 'ASCNetId=100')) ($c.Replace('ASCNetId=800', 'ASCNetId=100'))
Reject-Abuse 'wrong initial activation' ($s.Replace('Role=Server ASCNetId=800 Handle=10 Original=1', 'Role=Server ASCNetId=800 Handle=10 Original=2'))
Reject-Abuse 'incorrect replay message' ($s.Replace('Original=1 Message=2 Entries=1 Admitted=0', 'Original=1 Message=3 Entries=1 Admitted=0'))
Reject-Abuse 'rejected request entered consumer' ($s.Replace('Admitted=0 TargetValid=1', 'Admitted=1 TargetValid=1'))
Reject-Abuse 'forged target accepted' ($s.Replace('Admitted=1 TargetValid=0', 'Admitted=1 TargetValid=1'))
Reject-Abuse 'ability cancelled' ($s.Replace('Active=1 Events=0', 'Active=0 Events=0'))
Reject-Abuse 'malice triggered damage event' ($s.Replace('Active=1 Events=0', 'Active=1 Events=1'))
Reject-Abuse 'malice reduced HP' ($s.Replace('Health=1000000.000000 Effects=0', 'Health=999500.000000 Effects=0'))
Reject-Abuse 'malice applied effect' ($s.Replace('Health=1000000.000000 Effects=0', 'Health=1000000.000000 Effects=1'))
Reject-Abuse 'wrong target HP identity' ($s.Replace('Target=2 NetId=102', 'Target=2 NetId=103'))
Reject-Abuse 'duplicate HP checkpoint' ($s + "`nRPG_SKILL_ABUSE HP Index=0 Target=0 NetId=100 Health=1000000.000000 Effects=0")
Reject-Abuse 'duplicate sent case' $s ($c + "`n" + $clientAbuse[0])
Reject-Abuse 'recovery reused malicious key' ($s.Replace('Message=9 Valid=1', 'Message=8 Valid=1'))
Reject-Abuse 'recovery source invalid' ($s.Replace('Message=9 Valid=1', 'Message=9 Valid=0'))
Reject-Abuse 'recovery different handle' ($s.Replace('RECOVERY ASCNetId=800 Handle=10', 'RECOVERY ASCNetId=800 Handle=11'))
Reject-Abuse 'server runtime failure' ($s + "`nRPG_SKILL_ABUSE FAILED")
Reject-Abuse 'normal BP damage missing' ($s.Replace('RPG_SKILL_DAMAGE EFFECT', 'UNKNOWN EFFECT'))
Reject-Abuse 'normal BP cancelled' ($s.Replace('Cancelled=0', 'Cancelled=1'))
Reject-Abuse 'packet settings do not match' ($s.Replace('PktLoss set to 10', 'PktLoss set to 0'))
Reject-Abuse 'consumer never dispatched' ($s.Replace('RPG_SKILL_ABUSE_CONSUMER', 'UNKNOWN_CONSUMER'))
Reject-Abuse 'forged actor bypassed codec' ($s.Replace('KnownTag=1 Decoded=0 Validated=0', 'KnownTag=1 Decoded=1 Validated=0'))
Reject-Abuse 'out-of-range consumer approved' ($s.Replace('KnownTag=1 Decoded=1 Validated=0', 'KnownTag=1 Decoded=1 Validated=1'))
Reject-Abuse 'unknown tag accepted' ($s.Replace('KnownTag=0 Decoded=1 Validated=0', 'KnownTag=1 Decoded=1 Validated=0'))
Reject-Abuse 'consumer wrong ability handle' ($s.Replace('CONSUMER Handle=10', 'CONSUMER Handle=11'))
$earlyRecovery=[regex]::Match($s,'(?m)^RPG_SKILL_ABUSE RECOVERY[^\r\n]*').Value
Reject-Abuse 'recovery before hostile checkpoints' ($earlyRecovery + "`n" + $s.Replace($earlyRecovery,''))
$earlyReady=[regex]::Match($c,'(?m)^RPG_SKILL_ABUSE RELEASE_READY[^\r\n]*').Value
Reject-Abuse 'client release before hostile sends' $s ($earlyReady + "`n" + $c.Replace($earlyReady,''))
foreach ($extra in @(@{ClientCount=2},@{Runtime='Game'},@{VerifySkillProgression=$true},@{VerifySkillTripods=$true},
    @{VerifyTargetDataAbuse=$true},@{VerifyTargetDataRate=$true},@{VerifyTargetDataDamage=$true},@{VerifyMovementSecurity=$true})) {
    $options=@{VerifySkillTargetDataAbuse=$true;ClientCount=1;SkipBuild=$true;ProjectFile='Z:\absent.uproject'}
    foreach($key in $extra.Keys) { $options[$key]=$extra[$key] }
    $rejected=$false
    try { & (Join-Path $PSScriptRoot '..\Network\Run-IrisMultiClientTest.ps1') @options | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch 'Run authored skill damage separately') { throw }
        $rejected=$true
    }
    if(-not $rejected) { throw 'Invalid authored abuse CLI accepted.' }
    ++$script:abuseChecks
}
Write-Host "PASS: $script:abuseChecks authored TargetData abuse proof/CLI checks."
