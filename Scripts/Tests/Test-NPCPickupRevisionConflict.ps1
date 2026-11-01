$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../Network/PreparedPickupE2EProof.ps1')
. (Join-Path $PSScriptRoot '../Network/NPCPickupRevisionConflict.ps1')
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$allowedRoot=Join-Path $workspace 'Saved/BuildChecks/NPCPickupRevisionConflict-20261101/Temp'
$temporary=Join-Path $allowedRoot ([Guid]::NewGuid().ToString('N'))
$count=0
function Clone($Value){$Value | ConvertTo-Json -Depth 40 | ConvertFrom-Json}
function Rejected([string]$Name,[scriptblock]$Action){
 $rejected=$false;try{& $Action | Out-Null}catch{$rejected=$true}
 if(!$rejected){throw ('Accepted invalid revision evidence: '+$Name)}
 $script:count++
}
function New-State {
 $origin=[pscustomobject]@{requestId='11111111-1111-1111-1111-111111111111';commandSHA256=('a'*64);journalSHA256=('b'*64);prepareBodySHA256=('c'*64)}
 $entry=[pscustomobject]@{requestId=$origin.requestId;commandSHA256=$origin.commandSHA256;journalSHA256=$origin.journalSHA256;bodySHA256='e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';elapsedMilliseconds=40.0}
 $first=Clone $entry;$first | Add-Member status 'RevisionConflict';$first | Add-Member statusCode 409;$first | Add-Member acknowledgement 'delivered';$first | Add-Member bodyBytes 0;$first | Add-Member requestElapsedMilliseconds 80.0;$first.elapsedMilliseconds=100.0
 $second=Clone $first;$second.requestElapsedMilliseconds=2300.0;$second.elapsedMilliseconds=2400.0
 $prepare=[pscustomobject]@{status='Prepared';acknowledgement='delivered';consumed=$false;commandSHA256=$origin.commandSHA256;journalSHA256=$origin.journalSHA256;bodySHA256=$origin.prepareBodySHA256;elapsedMilliseconds=20.0}
 $again=Clone $prepare;$again.status='AlreadyPrepared';$again.elapsedMilliseconds=2200.0
 [pscustomobject]@{failure=$null;phase='revision-conflict-held';committed=$null;delivered=$null;droppedAcknowledgements=0;prepared=[pscustomobject]@{fault='consume-held';origin=$origin;consumes=@();preparations=@($prepare,$again);revisionConflict=[pscustomobject]@{phase='armed';setupBlocked=@($entry);heldConsumes=@();armed=[pscustomobject]@{elapsedMilliseconds=60.0};conflicts=@($first,$second)}}}
}
try {
 $null=New-Item -ItemType Directory -Path $temporary
 $state=New-State
 if((Get-NPCPickupRevisionConflictGap $state) -ne 2.2){throw 'Valid native retry gap rejected'};$count++
 foreach($case in @(
  @{name='wrong phase';change={param($s)$s.phase='recovered'}},
  @{name='proxy failed';change={param($s)$s.failure='Failed'}},
  @{name='missing origin';change={param($s)$s.prepared.origin.requestId=$null}},
  @{name='missing original digest';change={param($s)$s.prepared.origin.commandSHA256=$null}},
  @{name='out of band commit';change={param($s)$s.committed=[pscustomobject]@{status='Committed'}}},
  @{name='lost rather than delivered rejection';change={param($s)$s.prepared.revisionConflict.conflicts[0].acknowledgement='dropped'}},
  @{name='location conflict';change={param($s)$s.prepared.revisionConflict.conflicts[0].status='LocationConflict'}},
  @{name='HTTP 200';change={param($s)$s.prepared.revisionConflict.conflicts[0].statusCode=200}},
  @{name='missing body size';change={param($s)$s.prepared.revisionConflict.conflicts[0].bodyBytes=$null}},
  @{name='boolean body size';change={param($s)$s.prepared.revisionConflict.conflicts[0].bodyBytes=$false}},
  @{name='no native timer gap';change={param($s)$s.prepared.revisionConflict.conflicts[1].requestElapsedMilliseconds=200}},
  @{name='boolean request time';change={param($s)$s.prepared.revisionConflict.conflicts[1].requestElapsedMilliseconds=$true}},
  @{name='infinite response time';change={param($s)$s.prepared.revisionConflict.conflicts[1].elapsedMilliseconds=[double]::PositiveInfinity}},
  @{name='changed original request';change={param($s)$s.prepared.revisionConflict.conflicts[1].requestId='other'}},
  @{name='changed frozen body';change={param($s)$s.prepared.revisionConflict.conflicts[1].commandSHA256='other'}},
  @{name='changed journal';change={param($s)$s.prepared.revisionConflict.conflicts[1].journalSHA256='other'}},
  @{name='unexpected grant';change={param($s)$s.prepared.consumes=@([pscustomobject]@{status='Committed'})}},
  @{name='consumed prepare';change={param($s)$s.prepared.preparations[1].consumed=$true}},
  @{name='changed prepare body';change={param($s)$s.prepared.preparations[1].bodySHA256='other'}},
  @{name='string prepare time';change={param($s)$s.prepared.preparations[1].elapsedMilliseconds='2200'}}
 )){
  $invalid=New-State;& $case.change $invalid
  Rejected $case.name {Get-NPCPickupRevisionConflictGap $invalid}
 }
 $origin=[pscustomobject]@{requestId='11111111-1111-1111-1111-111111111111';dropId='22222222-2222-2222-2222-222222222222';itemId='33333333-3333-3333-3333-333333333333';characterId='44444444-4444-4444-4444-444444444444';sessionId='55555555-5555-5555-5555-555555555555';serverId='fixture'}
 $other='66666666-6666-6666-6666-666666666666'
 $command=[pscustomobject]@{requestId=$origin.requestId;operation='Item.Pickup';actor=[pscustomobject]@{type='Character';ownerId=$origin.characterId};mutations=@([pscustomobject]@{expectedRevision=0;newRecord=[pscustomobject]@{state=[pscustomobject]@{instanceId=$origin.itemId;quantity=1}}})}
 $row=[pscustomobject]@{request_id=$origin.requestId;drop_id=$origin.dropId;item_id=$origin.itemId;character_id=$origin.characterId;dungeon_session_id=$origin.sessionId;server_id=$origin.serverId;consumed_at=$null;command_payload=(Clone $command)}
 $snapshot=[pscustomobject]@{sessionOriginCount=1;origin=$row;item=[pscustomobject]@{item_id=$origin.itemId;owner_type='Character';owner_id=$other;revision=1;quantity=1;lifecycle_state='Active'};receipt=$null}
 $intent=[pscustomobject]@{status='Prepared';consumedAt=$null;dungeonSessionId=$origin.sessionId;serverId=$origin.serverId;dropId=$origin.dropId;requestId=$origin.requestId;characterId=$origin.characterId;itemId=$origin.itemId;command=(Clone $command)}
 Assert-NPCPickupRevisionUnconsumed $snapshot $intent $command $origin $other;$count++
 $empty=Clone $snapshot;$empty.item=$null
 Assert-NPCPickupRevisionUnconsumed $empty $intent $command $origin '';$count++
 Rejected 'missing conflicting item' {Assert-NPCPickupRevisionUnconsumed $empty $intent $command $origin $other}
 Rejected 'unexpected initial item' {Assert-NPCPickupRevisionUnconsumed $snapshot $intent $command $origin ''}
 foreach($case in @(
  @{name='item taken by claimant';change={param($s)$s.item.owner_id=$origin.characterId}},
  @{name='item revision advanced';change={param($s)$s.item.revision=2}},
  @{name='item removed';change={param($s)$s.item.lifecycle_state='Destroyed'}},
  @{name='item quantity changed';change={param($s)$s.item.quantity=2}},
  @{name='origin consumed';change={param($s)$s.origin.consumed_at='2026-11-01T00:00:00Z'}},
  @{name='receipt created';change={param($s)$s.receipt=[pscustomobject]@{status='Committed'}}},
  @{name='second reservation';change={param($s)$s.sessionOriginCount=2}},
  @{name='origin rebound';change={param($s)$s.origin.drop_id='other'}},
  @{name='changed full command';change={param($s)$s.origin.command_payload.mutations[0].expectedRevision=1}}
 )){
  $invalid=Clone $snapshot;& $case.change $invalid
  Rejected $case.name {Assert-NPCPickupRevisionUnconsumed $invalid $intent $command $origin $other}
 }
 $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../Network/Run-NPCPickupBackendE2ETest.ps1'),[ref]$null,[ref]$null)
 $guard=@($ast.EndBlock.Statements | Where-Object {$_.Extent.Text.StartsWith('if ($RevisionConflict ')})
 if($guard.Count -ne 1){throw 'Missing opt-in mode guard'}
 $validate=[scriptblock]::Create($ast.ParamBlock.Extent.Text+"`n"+$guard[0].Extent.Text)
 $valid=@{RevisionConflict=$true;LosePickupAck=$true;ClientCount=2}
 & $validate @valid;$count++
 foreach($override in @(@{LegacyProtocol=$true},@{SlotConflict=$true},@{LosePrepareAck=$true},@{RestartServerAfterLostAck=$true},@{LosePickupAck=$false},@{ClientCount=1})){
  $arguments=$valid.Clone();foreach($key in $override.Keys){$arguments[$key]=$override[$key]}
  Rejected 'incompatible mode' {& $validate @arguments}
 }
 [pscustomobject]@{passed=$true;cases=$count} | ConvertTo-Json
} finally {
 if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($temporary)) -ne [IO.Path]::GetFullPath($allowedRoot) -or [IO.Path]::GetFileName($temporary) -cnotmatch '^[a-f0-9]{32}$'){throw 'Revision fixture cleanup boundary mismatch'}
 . (Join-Path $PSScriptRoot '../Network/GuardianMelee2Artifacts.ps1')
 $cleanup=Remove-GuardianProbeArtifact $temporary $allowedRoot
 Write-Host ('REVISION_FIXTURE_CLEANUP deletedBytes='+$cleanup.deletedBytes+' remaining='+(Test-Path -LiteralPath $temporary))
}
