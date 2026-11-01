[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Test-SkillDamageProof.ps1')
. (Join-Path $PSScriptRoot '..\Network\SkillFinalCapProof.ps1')
$capProfileServer = 'RPG_SKILL_FINALCAP PROFILE Role=Server SourceASCNetId=800 Cap=625.000000 OriginalCap=1000000000.000000 Transient=1'
$capActive = 'RPG_SKILL_FINALCAP ACTIVE SourceASCNetId=800 Handle=10 Original=1 Cap=625.000000 SetByCaller=250.000000 Attack=2.500000 GlobalCap=1000000000.000000'
$capExecAccepted = 'RPG_SKILL_FINALCAP EXECUTION Index=0 SourceASCNetId=800 NetId=100 Handle=10 Original=1 Cap=625.000000 Calculated=625.000000 Bound=1 Approved=1'
$capExecRejected = 'RPG_SKILL_FINALCAP EXECUTION Index=4 SourceASCNetId=800 NetId=200 Handle=10 Original=1 Cap=625.000000 Calculated=1250.000000 Bound=1 Approved=0'
$capApplication = 'RPG_SKILL_FINALCAP APPLICATION SourceASCNetId=800 Handle=10 Original=1 AppliedTargets=1 AppliedHits=1 BudgetUsed=1 BudgetMax=64 ApprovedNetId=100 RejectedNetId=200'
$capServer = $capProfileServer + "`n" + $server.Replace('Attack=2.000000', 'Attack=2.500000').Replace('ExpectedDamage=500.000000','ExpectedDamage=625.000000').Replace('999500.000000','999375.000000')
$capServer = $capServer.Replace('RPG_SKILL_NETTEST RELEASE_SECTION Role=Server', $capActive + "`nRPG_SKILL_NETTEST RELEASE_SECTION Role=Server")
$capServer = $capServer.Replace('RPG_SKILL_DAMAGE POSE Index=0', $capExecAccepted + "`nRPG_SKILL_DAMAGE POSE Index=0")
$capServer = $capServer.Replace('RPG_SKILL_DAMAGE EVENT', (@(
    $capExecRejected,
    'RPG_SKILL_FINALCAP POSE Index=4 NetId=200 X=300.000000 Y=-120.000000 Z=0.000000 Friendly=0 Defense=0.500000',
    'RPG_SKILL_FINALCAP GE_CALLBACK Index=4 NetId=200 Count=1 SetByCaller=250.000000 Class=GE_Shared_ApplyDamage_C Health=1000000.000000',
    $capApplication,
    'RPG_SKILL_DAMAGE EVENT') -join "`n"))
$capServer = $capServer.Replace('RPG_SKILL_NETTEST SERVER_COMPLETE', (@(
    'RPG_SKILL_FINALCAP RESULT Role=Server Index=4 NetId=200 Before=1000000.000000 After=1000000.000000 Callbacks=1 HealthChanges=0',
    'RPG_SKILL_FINALCAP SERVER_COMPLETE Targets=5 Accepted=1 Rejected=1',
    'RPG_SKILL_NETTEST SERVER_COMPLETE') -join "`n"))
$capClient = $client.Replace('999500.000000','999375.000000').Replace('RPG_SKILL_NETTEST CLIENT_INPUT_STARTED', (@(
    'RPG_SKILL_FINALCAP PROFILE Role=Client SourceASCNetId=800 Cap=625.000000 OriginalCap=1000000000.000000 Transient=1',
    'RPG_SKILL_FINALCAP BASELINE Index=4 NetId=200 Health=1000000.000000',
    'RPG_SKILL_NETTEST CLIENT_INPUT_STARTED') -join "`n"))
$capClient = $capClient.Replace('RPG_SKILL_NETTEST CLIENT_COMPLETE', (@(
    'RPG_SKILL_FINALCAP RESULT Role=Client Index=4 NetId=200 Before=1000000.000000 After=1000000.000000 HealthChanges=0',
    'RPG_SKILL_FINALCAP CLIENT_COMPLETE Targets=5 ReplicatedHealth=1',
    'RPG_SKILL_NETTEST CLIENT_COMPLETE') -join "`n"))
$capProof = Get-SkillFinalCapProof $capServer $capClient 250 450 10
if (-not $capProof.finalPerSkillHealthDamageCapVerified -or -not $capProof.exactCapNormalHitVerified -or
    -not $capProof.serverHitBudgetRefundVerified -or $capProof.maliciousWireDamageVerified -or
    $capProof.shippingRuntimeVerified -or $capProof.actualPlayerASCNetRefId -ne 800 -or $capProof.rejectedTargetNetRefId -ne 200) {
    throw 'Final cap proof scope misread.'
}
$script:capChecks = 1
function Reject-Cap([string] $Name, [string] $S = $capServer, [string] $C = $capClient) {
    $rejected = $false
    try { Get-SkillFinalCapProof $S $C 250 450 10 | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid final cap evidence accepted: $Name" }
    ++$script:capChecks
}
foreach ($marker in @('PROFILE','ACTIVE','EXECUTION','POSE','GE_CALLBACK','APPLICATION','RESULT','SERVER_COMPLETE')) {
    Reject-Cap "missing server $marker" ($capServer.Replace("RPG_SKILL_FINALCAP $marker", "UNKNOWN $marker"))
}
foreach ($marker in @('PROFILE','BASELINE','RESULT','CLIENT_COMPLETE')) {
    Reject-Cap "missing client $marker" $capServer ($capClient.Replace("RPG_SKILL_FINALCAP $marker", "UNKNOWN $marker"))
}
foreach ($change in @(
    @('Bound=1','Bound=0'), @('Calculated=1250.000000','Calculated=625.000000'),
    @('Calculated=625.000000 Bound=1 Approved=1','Calculated=625.000000 Bound=1 Approved=0'),
    @('Calculated=1250.000000 Bound=1 Approved=0','Calculated=1250.000000 Bound=1 Approved=1'),
    @('SourceASCNetId=800 Handle=10','SourceASCNetId=801 Handle=10'),
    @('AppliedTargets=1','AppliedTargets=2'), @('AppliedHits=1','AppliedHits=2'), @('BudgetUsed=1','BudgetUsed=2'),
    @('ApprovedNetId=100','ApprovedNetId=200'), @('RejectedNetId=200','RejectedNetId=100'),
    @('HealthChanges=0','HealthChanges=1'), @('Callbacks=1','Callbacks=0'), @('Defense=0.500000','Defense=1.000000'),
    @('X=300.000000 Y=-120.000000','X=1500.000000 Y=-120.000000'), @('Friendly=0 Defense','Friendly=1 Defense'),
    @('Transient=1','Transient=0'), @('Cap=625.000000','Cap=1000000000.000000'),
    @('Index=4 SourceASCNetId=800 NetId=200 Handle=10 Original=1','Index=4 SourceASCNetId=800 NetId=200 Handle=11 Original=1'),
    @('Index=4 SourceASCNetId=800 NetId=200 Handle=10 Original=1','Index=4 SourceASCNetId=800 NetId=200 Handle=10 Original=2')
)) { Reject-Cap ($change -join ' -> ') ($capServer.Replace($change[0], $change[1])) }
Reject-Cap 'wrong client ASC' $capServer ($capClient.Replace('SourceASCNetId=800','SourceASCNetId=801'))
Reject-Cap 'wrong client target' $capServer ($capClient.Replace('Index=4 NetId=200','Index=4 NetId=201'))
Reject-Cap 'client HP changed' $capServer ($capClient.Replace('After=1000000.000000 HealthChanges=0','After=999999.000000 HealthChanges=1'))
Reject-Cap 'duplicate native decision' ($capServer + "`n" + $capExecRejected)
Reject-Cap 'malformed extra evidence' ($capServer + "`nRPG_SKILL_FINALCAP UNKNOWN")
Reject-Cap 'native decision before release' ($capExecRejected + "`n" + $capServer.Replace($capExecRejected,''))
Reject-Cap 'applied result before decisions' ($capApplication + "`n" + $capServer.Replace($capApplication,''))
Reject-Cap 'client baseline after input' $capServer ($capClient.Replace('RPG_SKILL_FINALCAP BASELINE Index=4 NetId=200 Health=1000000.000000','') + "`nRPG_SKILL_FINALCAP BASELINE Index=4 NetId=200 Health=1000000.000000")
Reject-Cap 'failure marker' ($capServer + "`nRPG_SKILL_FINALCAP FAILED Reason=test")
Reject-Cap 'mixed malicious probe' ($capServer + "`nRPG_SKILL_ABUSE SERVER_COMPLETE")
Reject-Cap 'packet override' ($capServer + "`nLogNet: PktLoss set to 0")
foreach ($extra in @(@{ClientCount=2},@{Runtime='Game'},@{VerifySkillMultiPeerAbuse=$true},@{VerifySkillTargetDataAbuse=$true},
    @{VerifySkillProgression=$true},@{VerifySkillTripods=$true},@{VerifyTargetDataDamage=$true},@{VerifyMovementSecurity=$true})) {
    $options = @{VerifySkillFinalCap=$true;ClientCount=1;SkipBuild=$true;ProjectFile='Z:\absent.uproject'}
    foreach ($key in $extra.Keys) { $options[$key] = $extra[$key] }
    $rejected = $false
    try { & (Join-Path $PSScriptRoot '..\Network\Run-IrisMultiClientTest.ps1') @options | Out-Null }
    catch { if ($_.Exception.Message -notmatch 'Run authored (final cap|skill damage) separately') { throw }; $rejected = $true }
    if (-not $rejected) { throw 'Invalid mixed final cap CLI accepted.' }
    ++$script:capChecks
}
Write-Host "PASS: $script:capChecks authored final cap proof/CLI checks (synthetic evidence only)."
