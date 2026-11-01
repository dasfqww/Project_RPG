[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Network\SkillDamageProof.ps1')
$packets = "LogNet: PktLoss set to 10`nLogNet: PktLagMin set to 250`nLogNet: PktLagMax set to 450"
$end = 'RPG_SKILL_END_TRACE Avatar=BP_Player_C_0 Ability=GA_Player_ChargeSlash_C_0 Original=1 Cancelled=0 Replicate=1 Active=1 Ending=0 NetSecurity=0'
$server = @"
$packets
RPG_NETTEST SERVER_STATE Iris=1 Connected=1 Expected=1 Revision=2
RPG_SKILL_NETTEST SERVER_ACTIVE Controller=BP_PlayerController_C_0 Ability=Default__GA_Player_ChargeSlash_C
RPG_SKILL_DAMAGE CONFIG Ability=GA_Player_ChargeSlash_C Radius=650 Angle=120 Base=250.000000 Scalar=1.000000 Attack=2.000000 Defense=1 Crit=0 ExpectedDamage=500.000000
RPG_SKILL_NETTEST RELEASE_SECTION Role=Server Controller=BP_PlayerController_C_0
RPG_SKILL_DAMAGE POSE Index=0 NetId=100 X=300.000000 Y=0.000000 Z=0.000000 Friendly=0
RPG_SKILL_DAMAGE POSE Index=1 NetId=101 X=-300.000000 Y=0.000000 Z=0.000000 Friendly=0
RPG_SKILL_DAMAGE POSE Index=2 NetId=102 X=300.000000 Y=120.000000 Z=0.000000 Friendly=1
RPG_SKILL_DAMAGE POSE Index=3 NetId=103 X=1500.000000 Y=0.000000 Z=0.000000 Friendly=0
RPG_SKILL_DAMAGE EFFECT Index=0 NetId=100 Count=1 SetByCaller=250.000000 Class=GE_Shared_ApplyDamage_C
RPG_SKILL_DAMAGE EVENT Tag=Player.Event.AOE Count=1
$end
RPG_SKILL_DAMAGE RESULT Role=Server Index=0 NetId=100 Before=1000000.000000 After=999500.000000 Effects=1
RPG_SKILL_DAMAGE RESULT Role=Server Index=1 NetId=101 Before=1000000.000000 After=1000000.000000 Effects=0
RPG_SKILL_DAMAGE RESULT Role=Server Index=2 NetId=102 Before=1000000.000000 After=1000000.000000 Effects=0
RPG_SKILL_DAMAGE RESULT Role=Server Index=3 NetId=103 Before=1000000.000000 After=1000000.000000 Effects=0
RPG_SKILL_DAMAGE SERVER_COMPLETE Targets=4 Events=1
RPG_SKILL_NETTEST SERVER_COMPLETE Controller=BP_PlayerController_C_0
"@
$client = @"
$packets
RPG_NETTEST CLIENT_READY ServerIris=1 ClientIris=1 Connected=1 Expected=1 Revision=2
RPG_SKILL_DAMAGE BASELINE Index=0 NetId=100 Health=1000000.000000
RPG_SKILL_DAMAGE BASELINE Index=1 NetId=101 Health=1000000.000000
RPG_SKILL_DAMAGE BASELINE Index=2 NetId=102 Health=1000000.000000
RPG_SKILL_DAMAGE BASELINE Index=3 NetId=103 Health=1000000.000000
RPG_SKILL_NETTEST CLIENT_INPUT_STARTED Action=IA_IdentitySkill
RPG_SKILL_NETTEST CLIENT_ACTIVE Ability=Default__GA_Player_ChargeSlash_C
RPG_SKILL_NETTEST CLIENT_INPUT_STOPPED
RPG_SKILL_NETTEST RELEASE_SECTION Role=Client Controller=BP_PlayerController_C_0
RPG_SKILL_NETTEST CLIENT_RELEASED
RPG_SKILL_DAMAGE HP_NOTIFY Index=0 NetId=100 Old=1000000.000000 New=999500.000000
$end
RPG_SKILL_DAMAGE RESULT Role=Client Index=0 NetId=100 Before=1000000.000000 After=999500.000000 Notify=1
RPG_SKILL_DAMAGE RESULT Role=Client Index=1 NetId=101 Before=1000000.000000 After=1000000.000000 Notify=0
RPG_SKILL_DAMAGE RESULT Role=Client Index=2 NetId=102 Before=1000000.000000 After=1000000.000000 Notify=0
RPG_SKILL_DAMAGE RESULT Role=Client Index=3 NetId=103 Before=1000000.000000 After=1000000.000000 Notify=0
RPG_SKILL_DAMAGE CLIENT_COMPLETE Targets=4 ReplicatedHealth=1
RPG_SKILL_NETTEST CLIENT_COMPLETE
"@
$proof = Get-SkillDamageProof $server $client 250 450 10
if ($proof.schemaVersion -ne 1 -or $proof.targets.Count -ne 4 -or $proof.expectedHealthReduction -ne 500 -or
    -not $proof.authoredInputMontageDamageVerified -or -not $proof.clientAttributeNotificationAndReplicationVerified -or
    $proof.maliciousWireDamageVerified -or $proof.multiPeerDamageIsolationVerified -or $proof.shippingRuntimeVerified -or
    $proof.finalPerSkillHealthDamageCapVerified -or $proof.productionNpcLifecycleVerified -or $proof.renderedHealthUiVerified) {
    throw 'Golden evidence/scope misread.'
}
$script:passed = 1
function Reject([string] $Name, [string] $S = $server, [string] $C = $client) {
    $rejected = $false
    try { Get-SkillDamageProof $S $C 250 450 10 | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid damage evidence accepted: $Name" }
    ++$script:passed
}
foreach ($marker in @('CONFIG', 'EVENT', 'POSE', 'EFFECT', 'RESULT', 'SERVER_COMPLETE')) {
    Reject "server missing $marker" ($server.Replace("RPG_SKILL_DAMAGE $marker", "UNKNOWN $marker"))
}
foreach ($marker in @('BASELINE', 'HP_NOTIFY', 'RESULT', 'CLIENT_COMPLETE')) {
    Reject "client missing $marker" $server ($client.Replace("RPG_SKILL_DAMAGE $marker", "UNKNOWN $marker"))
}
Reject 'cancelled server' ($server.Replace('Cancelled=0', 'Cancelled=1'))
Reject 'missing server Iris' ($server.Replace('RPG_NETTEST SERVER_STATE', 'UNKNOWN'))
Reject 'client Iris disabled' $server ($client.Replace('ClientIris=1', 'ClientIris=0'))
Reject 'activation identity mismatch' $server ($client.Replace('Original=1', 'Original=2'))
Reject 'cancelled client' $server ($client.Replace('Cancelled=0', 'Cancelled=1'))
Reject 'wrong damage scalar' ($server.Replace('Scalar=1.000000', 'Scalar=2.000000'))
Reject 'wrong damage magnitude' ($server.Replace('SetByCaller=250.000000', 'SetByCaller=251.000000'))
Reject 'wrong GE' ($server.Replace('Class=GE_Shared_ApplyDamage_C', 'Class=Fake_C'))
Reject 'wrong server HP' ($server.Replace('After=999500.000000', 'After=999499.000000'))
Reject 'wrong client HP' $server ($client.Replace('After=999500.000000', 'After=999499.000000'))
Reject 'wrong HP callback' $server ($client.Replace('New=999500.000000', 'New=999499.000000'))
Reject 'wrong initial HP' $server ($client.Replace('Health=1000000.000000', 'Health=1.000000'))
Reject 'lost callback' $server ($client.Replace('Notify=1', 'Notify=0'))
Reject 'guard damaged' ($server.Replace('After=1000000.000000 Effects=0', 'After=999500.000000 Effects=1'))
Reject 'duplicate index' ($server.Replace('Index=1', 'Index=0'))
Reject 'duplicate NetId' ($server.Replace('NetId=101', 'NetId=100'))
Reject 'client NetId mismatch' $server ($client.Replace('NetId=100', 'NetId=999'))
Reject 'zero NetId' ($server.Replace('NetId=100', 'NetId=0'))
Reject 'guard inside sector' ($server.Replace('X=-300.000000', 'X=300.000000'))
Reject 'far guard in range' ($server.Replace('X=1500.000000', 'X=300.000000'))
Reject 'wrong team' ($server.Replace('Friendly=1', 'Friendly=0'))
Reject 'malformed extra marker' ($server + "`nRPG_SKILL_DAMAGE POSE Index=9")
Reject 'unknown extra marker' ($server + "`nRPG_SKILL_DAMAGE UNKNOWN")
Reject 'duplicate event' ($server + "`nRPG_SKILL_DAMAGE EVENT Tag=Player.Event.AOE Count=1")
Reject 'late packet override server' ($server + "`nLogNet: PktLoss set to 0")
Reject 'late packet override client' $server ($client + "`nLogNet: PktLagMin set to 0")
Reject 'missing applied packet settings' ($server.Replace('LogNet: PktLagMax', 'UNKNOWN'))
Reject 'ensure' ($server + "`nEnsure condition failed: test")
Reject 'fatal' $server ($client + "`nFatal error: test")
Reject 'runtime failure' ($server + "`nRPG_SKILL_DAMAGE FAILED Reason=test")
Reject 'client input before baseline' $server ($client.Replace('RPG_SKILL_NETTEST CLIENT_INPUT_STARTED Action=IA_IdentitySkill', '').Insert(0, "RPG_SKILL_NETTEST CLIENT_INPUT_STARTED Action=IA_IdentitySkill`n"))
# CLI rejections must happen before resolving paths, building or starting any process.
$runner = Join-Path $PSScriptRoot '..\Network\Run-IrisMultiClientTest.ps1'
foreach ($extra in @(@{ClientCount=2}, @{Runtime='Game'}, @{VerifySkillProgression=$true}, @{VerifySkillTripods=$true},
    @{VerifyTargetDataAbuse=$true}, @{VerifyTargetDataRate=$true}, @{VerifyMovementSecurity=$true},
    @{VerifyAuthorizedMovement=$true}, @{VerifyAuthoredRoll=$true})) {
    $args = @{VerifySkillDamage=$true; ClientCount=1; SkipBuild=$true; ProjectFile='Z:\absent.uproject'}
    foreach ($key in $extra.Keys) { $args[$key] = $extra[$key] }
    $rejected = $false
    try { & $runner @args } catch {
        if ($_.Exception.Message -notmatch 'Run authored skill damage separately') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Invalid mixed damage CLI accepted.' }
    ++$script:passed
}
Write-Host "PASS: $script:passed skill damage proof/CLI checks (synthetic evidence, not runtime damage)."
