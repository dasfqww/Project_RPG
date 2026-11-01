[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Test-SkillMultiPeerAbuseProof.ps1')
. (Join-Path $PSScriptRoot 'Test-SkillFinalCapProof.ps1')

function Add-EndOrigin([string] $Text, [string] $Role, [int] $ASC = 800, [int] $Handle = 10,
    [int] $Peer = 0, [switch] $Remote) {
    $prefix = if ($Peer) { "RPG_SKILL_PEER Peer=$Peer " } else { '' }
    $marker = $prefix + 'RPG_SKILL_END_TRACE'
    $source = if ($Role -eq 'Server') { 'AuthorityLocal' } elseif ($Remote) { 'AuthorityRPC' } else { 'ClientLocal' }
    $matched = if ($Remote) { 1 } else { 0 }
    $rpcKey = if ($Remote) { 1 } else { 0 }
    $row = "${prefix}RPG_SKILL_END_ORIGIN Role=$Role ASCNetId=$ASC Handle=$Handle Original=1 Source=$source RpcMatched=$matched RpcOriginal=$rpcKey"
    $result = $Text.Replace($marker, $row + "`n" + $marker)
    if ($Remote) { $result = $result.Replace('Cancelled=0 Replicate=1', 'Cancelled=0 Replicate=0') }
    return $result
}
$originServer = Add-EndOrigin $server Server
$originClient = Add-EndOrigin $client Client
$originRemote = Add-EndOrigin $client Client -Remote
$script:originChecks = 0
foreach ($fixture in @(@{ text=$originClient; source='ClientLocal' }, @{ text=$originRemote; source='AuthorityRPC' })) {
    $result = Get-SkillDamageProof $originServer $fixture.text 250 450 10 -RequireEndOrigin
    if (-not $result.abilityEndOriginVerified -or $result.clientAbilityEndOrigin -ne $fixture.source -or
        $result.actualEndSourceASCNetRefId -ne 800 -or $result.actualEndAbilityHandle -ne 10) { throw 'End provenance golden/scope mismatch.' }
    ++$script:originChecks
}
$legacy = Get-SkillDamageProof $server $client 250 450 10
if ($legacy.abilityEndOriginVerified -or $legacy.clientAbilityEndOrigin -ne 'LegacyUnclassified') { throw 'Legacy logs gained unobserved provenance.' }
++ $script:originChecks

function Reject-Origin([string] $Name, [string] $S = $originServer, [string] $C = $originRemote,
    [bool] $Required = $true) {
    $rejected = $false
    try { Get-SkillDamageProof $S $C 250 450 10 -RequireEndOrigin:$Required | Out-Null }
    catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid end provenance accepted: $Name" }
    ++$script:originChecks
}
Reject-Origin 'old DLL rejected by mandatory live runner' $server $client
Reject-Origin 'legacy non-replicated end still unproven' $server ($client.Replace('Replicate=1', 'Replicate=0')) $false
Reject-Origin 'server origin missing' $server
Reject-Origin 'client origin missing' $originServer ($client.Replace('Replicate=1', 'Replicate=0'))
Reject-Origin 'partial provenance even in legacy mode' $originServer $client $false
Reject-Origin 'server remote end forbidden' ($originServer.Replace('Source=AuthorityLocal RpcMatched=0 RpcOriginal=0', 'Source=AuthorityRPC RpcMatched=1 RpcOriginal=1').Replace('Replicate=1', 'Replicate=0'))
Reject-Origin 'server receipt claims local with replicate zero' ($originServer.Replace('Replicate=1', 'Replicate=0'))
Reject-Origin 'remote end has no matching receipt' $originServer ($originRemote.Replace('RpcMatched=1', 'RpcMatched=0'))
Reject-Origin 'remote RPC has different prediction key' $originServer ($originRemote.Replace('RpcOriginal=1', 'RpcOriginal=2'))
Reject-Origin 'remote RPC has zero prediction key' $originServer ($originRemote.Replace('RpcOriginal=1', 'RpcOriginal=0'))
Reject-Origin 'remote end relays back to authority' $originServer ($originRemote.Replace('Replicate=0', 'Replicate=1'))
Reject-Origin 'remote end cancelled' $originServer ($originRemote.Replace('Cancelled=0', 'Cancelled=1'))
Reject-Origin 'local origin cannot justify remote end' $originServer ($originClient.Replace('Replicate=1', 'Replicate=0'))
Reject-Origin 'local origin cannot borrow RPC receipt' $originServer ($originClient.Replace('RpcMatched=0 RpcOriginal=0', 'RpcMatched=1 RpcOriginal=1'))
Reject-Origin 'client wrongly labelled authority local' $originServer ($originClient.Replace('Source=ClientLocal', 'Source=AuthorityLocal'))
Reject-Origin 'unknown origin' $originServer ($originRemote.Replace('Source=AuthorityRPC', 'Source=Unknown'))
Reject-Origin 'wrong origin role' $originServer ($originRemote.Replace('Role=Client ASCNetId', 'Role=Server ASCNetId'))
Reject-Origin 'wrong origin ASC' $originServer ($originRemote.Replace('ASCNetId=800', 'ASCNetId=801'))
Reject-Origin 'wrong origin handle' $originServer ($originRemote.Replace('Handle=10', 'Handle=11'))
Reject-Origin 'zero origin ASC' $originServer ($originRemote.Replace('ASCNetId=800', 'ASCNetId=0'))
Reject-Origin 'zero origin handle' $originServer ($originRemote.Replace('Handle=10', 'Handle=0'))
Reject-Origin 'wrong origin key' $originServer ($originRemote.Replace('Handle=10 Original=1', 'Handle=10 Original=2'))
Reject-Origin 'zero end key' $originServer ($originRemote.Replace('Original=1', 'Original=0'))
Reject-Origin 'end key exceeds signed range' ($originServer.Replace('Original=1', 'Original=32768')) ($originRemote.Replace('Original=1', 'Original=32768'))
Reject-Origin 'inactive end' $originServer ($originRemote.Replace('Active=1 Ending=0', 'Active=0 Ending=0'))
Reject-Origin 'reentrant end' $originServer ($originRemote.Replace('Ending=0', 'Ending=1'))
Reject-Origin 'wrong security policy' $originServer ($originRemote.Replace('NetSecurity=0', 'NetSecurity=1'))
Reject-Origin 'target identity borrowed' ($originServer.Replace('ASCNetId=800', 'ASCNetId=100')) ($originRemote.Replace('ASCNetId=800', 'ASCNetId=100'))
$originRow = [regex]::Match($originRemote, '(?m)^RPG_SKILL_END_ORIGIN[^\r\n]*').Value
Reject-Origin 'duplicate origin' $originServer ($originRemote + "`n" + $originRow)
Reject-Origin 'extra malformed origin' $originServer ($originRemote + "`nRPG_SKILL_END_ORIGIN UNKNOWN")
Reject-Origin 'origin after actual end' $originServer ($originRemote.Replace($originRow,'') + "`n" + $originRow)
Reject-Origin 'origin before authored release' $originServer ($originRow + "`n" + $originRemote.Replace($originRow,''))
Reject-Origin 'duplicate actual end' $originServer ($originRemote + "`n" + $end.Replace('Replicate=1','Replicate=0'))

$originAbuseServer = Add-EndOrigin $s Server
$originAbuseClient = Add-EndOrigin $c Client -Remote
$abuse = Get-SkillTargetDataAbuseProof $originAbuseServer $originAbuseClient 250 450 10 -RequireEndOrigin
if (-not $abuse.abilityEndOriginVerified -or $abuse.clientAbilityEndOrigin -ne 'AuthorityRPC') { throw 'Abuse wrapper lost actual end provenance.' }
++ $script:originChecks
foreach ($replacement in @(@{ old='ASCNetId=800'; value='ASCNetId=801' }, @{ old='Handle=10'; value='Handle=11' })) {
    $badS = $originAbuseServer.Replace('RPG_SKILL_END_ORIGIN Role=Server ASCNetId=800 Handle=10',
        ('RPG_SKILL_END_ORIGIN Role=Server ASCNetId=800 Handle=10').Replace($replacement.old,$replacement.value))
    $badC = $originAbuseClient.Replace('RPG_SKILL_END_ORIGIN Role=Client ASCNetId=800 Handle=10',
        ('RPG_SKILL_END_ORIGIN Role=Client ASCNetId=800 Handle=10').Replace($replacement.old,$replacement.value))
    $rejected = $false
    try { Get-SkillTargetDataAbuseProof $badS $badC 250 450 10 -RequireEndOrigin | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw 'End provenance borrowed a different wire activation.' }
    ++$script:originChecks
}

$originCapServer = Add-EndOrigin $capServer Server
$originCapClient = Add-EndOrigin $capClient Client -Remote
$cap = Get-SkillFinalCapProof $originCapServer $originCapClient 250 450 10 -RequireEndOrigin
if (-not $cap.abilityEndOriginVerified -or -not $cap.finalPerSkillHealthDamageCapVerified) { throw 'Final-cap wrapper lost end provenance or cap proof.' }
++ $script:originChecks
$badCapS = $originCapServer.Replace('END_ORIGIN Role=Server ASCNetId=800 Handle=10','END_ORIGIN Role=Server ASCNetId=801 Handle=10')
$badCapC = $originCapClient.Replace('END_ORIGIN Role=Client ASCNetId=800 Handle=10','END_ORIGIN Role=Client ASCNetId=801 Handle=10')
$rejected = $false
try { Get-SkillFinalCapProof $badCapS $badCapC 250 450 10 -RequireEndOrigin | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'Final cap activation borrowed an end receipt.' }
++ $script:originChecks

$originMultiServer = Add-EndOrigin (Add-EndOrigin $multiServer Server 800 10 256) Server 900 20 257
$originMultiClients = @((Add-EndOrigin $clients[0] Client 800 10 256 -Remote), (Add-EndOrigin $clients[1] Client 900 20 257))
$multi = Get-SkillMultiPeerAbuseProof $originMultiServer $originMultiClients 250 450 10 -RequireEndOrigin
if (-not $multi.abilityEndOriginVerified -or @($multi.peers | Where-Object { $_.clientAbilityEndOrigin -eq 'AuthorityRPC' }).Count -ne 1) { throw 'Multi-peer wrapper lost per-source provenance.' }
++ $script:originChecks
function Reject-MultiOrigin([string] $Name, [string] $S = $originMultiServer, [string[]] $C = $originMultiClients) {
    $rejected = $false
    try { Get-SkillMultiPeerAbuseProof $S $C 250 450 10 -RequireEndOrigin | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid peer end provenance accepted: $Name" }
    ++$script:originChecks
}
Reject-MultiOrigin 'unscoped origin' ($originMultiServer.Replace('RPG_SKILL_PEER Peer=256 RPG_SKILL_END_ORIGIN', 'RPG_SKILL_END_ORIGIN'))
Reject-MultiOrigin 'unknown peer origin' ($originMultiServer.Replace('RPG_SKILL_PEER Peer=256 RPG_SKILL_END_ORIGIN','RPG_SKILL_PEER Peer=999 RPG_SKILL_END_ORIGIN'))
Reject-MultiOrigin 'borrowed other peer receipt' $originMultiServer @($originMultiClients[0].Replace('END_ORIGIN Role=Client ASCNetId=800 Handle=10','END_ORIGIN Role=Client ASCNetId=900 Handle=20'), $originMultiClients[1])
Reject-MultiOrigin 'missing second source end receipt' ($originMultiServer.Replace('RPG_SKILL_PEER Peer=257 RPG_SKILL_END_ORIGIN','UNKNOWN'))
Reject-MultiOrigin 'first peer cancels' $originMultiServer @($originMultiClients[0].Replace('Cancelled=0','Cancelled=1'),$originMultiClients[1])
Reject-MultiOrigin 'second peer cancels' $originMultiServer @($originMultiClients[0],$originMultiClients[1].Replace('Cancelled=0','Cancelled=1'))
Reject-MultiOrigin 'malice damages HP despite good end provenance' ($originMultiServer.Replace('Health=1000000.000000 Effects=0','Health=999500.000000 Effects=1'))
Reject-MultiOrigin 'authority end receipt late' ($originMultiServer + "`nRPG_SKILL_PEER Peer=256 RPG_SKILL_END_ORIGIN Role=Server ASCNetId=800 Handle=10 Original=1 Source=AuthorityLocal RpcMatched=0 RpcOriginal=0")

# Only this explicit fixture may delay client presentation; both real receipt scopes are required.
function Add-EndRace([string] $Text, [int] $Peer, [int] $Handle) {
    $marker = "RPG_SKILL_PEER Peer=$Peer RPG_SKILL_NETTEST CLIENT_RELEASED"
    $row = "RPG_SKILL_PEER Peer=$Peer RPG_SKILL_END_RACE CLIENT_MONTAGE_DELAY Handle=$Handle Original=1 Before=1.000000 After=0.500000 Fixture=1"
    return $Text.Replace($marker, $row + "`n" + $marker)
}
$raceClients = @((Add-EndRace $originMultiClients[0] 256 10),
    (Add-EndRace (Add-EndOrigin $clients[1] Client 900 20 257 -Remote) 257 20))
$race = Get-SkillMultiPeerAbuseProof $originMultiServer $raceClients 250 450 10 -RequireEndOrigin -RequireEndRaceFixture
if (-not $race.delayedClientMontageFixture -or -not $race.authorityRpcEndOnBothClientsVerified -or
    @($race.peers | Where-Object { $_.clientAbilityEndOrigin -ne 'AuthorityRPC' }).Count -gt 0) { throw 'Explicit delayed fixture lost both authority receipts.' }
++ $script:originChecks
if ($multi.delayedClientMontageFixture -or $multi.authorityRpcEndOnBothClientsVerified) { throw 'Ordinary mixed-end run gained delayed fixture evidence.' }
++ $script:originChecks
function Reject-EndRace([string] $Name, [string] $S = $originMultiServer, [string[]] $C = $raceClients,
    [bool] $Required = $true) {
    $rejected = $false
    try { Get-SkillMultiPeerAbuseProof $S $C 250 450 10 -RequireEndOrigin -RequireEndRaceFixture:$Required | Out-Null }
    catch { $rejected = $true }
    if (-not $rejected) { throw "Invalid delayed montage fixture accepted: $Name" }
    ++$script:originChecks
}
Reject-EndRace 'fixture cannot claim an ordinary baseline' $originMultiServer $raceClients $false
Reject-EndRace 'both delay observations missing' $originMultiServer $originMultiClients
Reject-EndRace 'second delay observation missing' $originMultiServer @($raceClients[0], (Add-EndOrigin $clients[1] Client 900 20 257 -Remote))
Reject-EndRace 'second client still ends locally' $originMultiServer @($raceClients[0], (Add-EndRace $originMultiClients[1] 257 20))
foreach ($replacement in @(
    @{old='Before=1.000000';value='Before=2.000000'}, @{old='After=0.500000';value='After=0.250000'},
    @{old='Before=1.000000';value='Before=1x000000'}, @{old='After=0.500000';value='After=0x500000'},
    @{old='Fixture=1';value='Fixture=0'}, @{old='DELAY Handle=10';value='DELAY Handle=11'},
    @{old='DELAY Handle=10 Original=1';value='DELAY Handle=10 Original=2'},
    @{old='Peer=256 RPG_SKILL_END_RACE';value='Peer=257 RPG_SKILL_END_RACE'},
    @{old='Peer=256 RPG_SKILL_END_RACE';value='Peer=999 RPG_SKILL_END_RACE'},
    @{old='RPG_SKILL_PEER Peer=256 RPG_SKILL_END_RACE';value='RPG_SKILL_END_RACE'})) {
    Reject-EndRace "bad delay row: $($replacement.old) -> $($replacement.value)" $originMultiServer @($raceClients[0].Replace($replacement.old,$replacement.value),$raceClients[1])
}
$delayRow = [regex]::Match($raceClients[0], '(?m)^RPG_SKILL_PEER Peer=256 RPG_SKILL_END_RACE[^\r\n]*').Value
Reject-EndRace 'duplicate delay row' $originMultiServer @($raceClients[0] + "`n" + $delayRow, $raceClients[1])
Reject-EndRace 'malformed extra fixture row' $originMultiServer @($raceClients[0] + "`nRPG_SKILL_PEER Peer=256 RPG_SKILL_END_RACE UNKNOWN", $raceClients[1])
Reject-EndRace 'delay before release section' $originMultiServer @($delayRow + "`n" + $raceClients[0].Replace($delayRow,''),$raceClients[1])
Reject-EndRace 'delay cannot borrow the other peer release order' $originMultiServer @(
    "RPG_SKILL_PEER Peer=257 RPG_SKILL_NETTEST RELEASE_SECTION Role=Client Controller=BP_PlayerController_C_1`n" + $delayRow + "`n" + $raceClients[0].Replace($delayRow,''),$raceClients[1])
Reject-EndRace 'delay after actual end' $originMultiServer @($raceClients[0].Replace($delayRow,'') + "`n" + $delayRow,$raceClients[1])
Reject-EndRace 'authority must never execute local fixture' ($originMultiServer + "`n" + $delayRow)
Reject-Origin 'ordinary normal proof rejects delayed fixture' $originServer ($originRemote + "`n" + $delayRow)
foreach ($wrapper in @('Abuse','Cap')) {
    $rejected = $false
    try {
        if ($wrapper -eq 'Abuse') { Get-SkillTargetDataAbuseProof $originAbuseServer ($originAbuseClient + "`n" + $delayRow) 250 450 10 -RequireEndOrigin | Out-Null }
        else { Get-SkillFinalCapProof $originCapServer ($originCapClient + "`n" + $delayRow) 250 450 10 -RequireEndOrigin | Out-Null }
    } catch { $rejected = $true }
    if (-not $rejected) { throw "$wrapper proof silently accepted the delayed fixture." }
    ++$script:originChecks
}
foreach ($invalid in @(@{ClientCount=1}, @{Runtime='Game'}, @{VerifySkillFinalCap=$true},
    @{VerifySkillTripods=$true}, @{VerifySkillProgression=$true}, @{VerifyMovementSecurity=$true},
    @{VerifyTargetDataAbuse=$true}, @{VerifyAuthorizedMovement=$true}, @{VerifyAuthoredRoll=$true})) {
    $rejected = $false
    try { & (Join-Path $PSScriptRoot '..\Network\Run-IrisMultiClientTest.ps1') -VerifySkillEndRace @invalid -SkipBuild -ProjectFile 'Z:\absent.uproject' | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch 'Run authored (skill damage|final cap) separately') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Invalid delayed fixture CLI scope accepted.' }
    ++$script:originChecks
}
foreach ($limit in @(1,9,241)) {
    $rejected = $false
    try { & (Join-Path $PSScriptRoot '..\Network\Run-IrisMultiClientTest.ps1') -ClientMaxFPS $limit -SkipBuild -ProjectFile 'Z:\absent.uproject' | Out-Null }
    catch { if ($_.Exception.Message -notmatch 'ClientMaxFPS') { throw }; $rejected = $true }
    if (-not $rejected) { throw 'Invalid client frame limit accepted.' }
    ++$script:originChecks
}
Write-Host "PASS: $script:originChecks ability-end provenance checks (synthetic evidence only; no unproven remote end acceptance)."
