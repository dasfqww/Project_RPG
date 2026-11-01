[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunRoot,[switch]$WriteProof)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$allowed=[IO.Path]::GetFullPath((Join-Path $projectRoot 'Saved/Tests/DungeonMembershipBackend'))+[IO.Path]::DirectorySeparatorChar
$run=[IO.Path]::GetFullPath($RunRoot)
if (-not $run.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path $run -Leaf) -cnotmatch '^[0-9a-f]{32}$') { throw 'Use an existing owned membership-test run directory.' }
. (Join-Path $PSScriptRoot '../Network/DungeonPartyHudObservation.ps1')
$sourcePath=Join-Path $run 'DungeonReconnectWithinGrace.proof.json'
if (-not (Test-Path -LiteralPath $sourcePath)) { $sourcePath=Join-Path $run 'Failed.proof.json' }
$runtime=Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($runtime.runId -cne (Split-Path $run -Leaf)) { throw 'Run identity mismatch.' }
$reconnect=$runtime.unexpectedDisconnect
if (-not $reconnect.ownedClientProcessKilled -or -not $reconnect.actualFreshTicketAdmission -or
    -not $reconnect.currentConfirmationFromUnrealServer -or -not $reconnect.partyRetained -or
    -not $reconnect.staleReportRejected403 -or -not $reconnect.staleConfirmationRejected403 -or
    $reconnect.clockOverride -or $reconnect.playerAbandonment -or $reconnect.expulsionCount -ne 0 -or
    [long]$reconnect.newGeneration -ne ([long]$reconnect.originalGeneration+1)) {
    throw 'Real owned-process loss and fresh authenticated reconnect evidence is missing.'
}
$module=Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-Project_RPG.dll'
if ((Get-FileHash -LiteralPath $module).Hash -cne $runtime.moduleSHA256) { throw 'Recorded binary differs; do not promote historical evidence to the current binary.' }
foreach($inputFile in $runtime.sourceAndRunnerHashes) {
    if ((Get-FileHash -LiteralPath $inputFile.path).Hash -cne $inputFile.SHA256) {
        throw "A recorded input differs: $($inputFile.path)"
    }
}
$prefix='Member'+$runtime.runId.Substring(0,10)
$names=@(($prefix+'1'),($prefix+'2'))
$samples=@(
    @{client='client-1';phase='initial';self=$names[0];grace=''},
    @{client='client-2';phase='initial';self=$names[1];grace=''},
    @{client='client-2';phase='grace';self=$names[1];grace=$names[0]},
    @{client='client-2';phase='reconnected';self=$names[1];grace=''},
    @{client='client-reconnect';phase='reconnected';self=$names[0];grace=''}
)
$vitals=$runtime.partyCombatHud.vitals
if ($vitals) {
    foreach ($field in 'initialTargetHealth','damagedTargetHealth','incapacitatedTargetHealth','survivorHealth') {
        $value=[double]$vitals.$field
        if ($null -eq $vitals.$field -or [double]::IsNaN($value) -or [double]::IsInfinity($value)) {
            throw 'Missing or non-finite authoritative vitals metadata.'
        }
    }
    if (-not $vitals.authorizedServerDamage -or $vitals.directHealthWrite -or
        -not $vitals.damageAndDeathRenderedOnBothClients -or $vitals.naturalCombat -or
        [double]$vitals.initialTargetHealth -le [double]$vitals.damagedTargetHealth -or
        [double]$vitals.damagedTargetHealth -le 0 -or [double]$vitals.incapacitatedTargetHealth -ne 0 -or
        [double]$vitals.survivorHealth -le 0) { throw 'Invalid authorized vitals fixture metadata.' }
    $expectedDamage=@{}; $expectedDamage[$names[0]]=[double]$vitals.damagedTargetHealth
    $expectedDamage[$names[1]]=[double]$vitals.survivorHealth
    $expectedDeath=@{}; $expectedDeath[$names[0]]=0
    $expectedDeath[$names[1]]=[double]$vitals.survivorHealth
    $expectedInitial=@{}; $expectedInitial[$names[0]]=[double]$vitals.initialTargetHealth
    $expectedInitial[$names[1]]=[double]$vitals.survivorHealth
    $samples[0].expected=$expectedInitial; $samples[1].expected=$expectedInitial
    $samples+=@(
        @{client='client-1';phase='damaged';self=$names[0];grace='';expected=$expectedDamage},
        @{client='client-2';phase='damaged';self=$names[1];grace='';expected=$expectedDamage},
        @{client='client-1';phase='incapacitated';self=$names[0];grace='';dead=$names[0];expected=$expectedDeath},
        @{client='client-2';phase='incapacitated';self=$names[1];grace='';dead=$names[0];expected=$expectedDeath}
    )
}
$observations=@(foreach($sample in $samples) {
    $stem='party-hud-'+$sample.phase+'-'+$sample.client
    $path=Join-Path $run ($stem+'.txt')
    $snapshot=Get-Content -LiteralPath $path -Raw -Encoding UTF8
    # Unique designer-owned variant text distinguishes this WBP from native
    # fallback, which intentionally formats the same public member data.
    $variantNote=[regex]::Unescape('\uC811\uC18D\u00B7\uD0C8\uD1F4 \uC0C1\uD0DC\uB294 \uC11C\uBC84 \uD655\uC778 \uAE30\uC900')
    if (-not $snapshot.Contains($variantNote)) { throw "Expected left-centre WBP is missing: $stem" }
    $expected=if ($sample.expected) { $sample.expected } else { @{} }
    $evidence=Get-PartyHudSlateEvidence $snapshot $names $sample.self $sample.grace ([string]$sample.dead) $expected
    if (-not $evidence) { throw "Captured HUD state is invalid: $stem" }
    $evidence.client=$sample.client
    $evidence.phase=$sample.phase
    $evidence.snapshot=$path
    $evidence.snapshotSHA256=(Get-FileHash -LiteralPath $path).Hash
    $image=Join-Path $run ($stem+'.png')
    if (Test-Path -LiteralPath $image) {
        $evidence.image=$image
        $evidence.imageSHA256=(Get-FileHash -LiteralPath $image).Hash
    }
    $evidence
})
$result=[ordered]@{
    schemaVersion=1;passed=$true;scope='Saved real two-client party HUD observations only';runId=$runtime.runId;
    renderedSlateObservations=$observations.Count;observations=$observations;
    actualFreshTicketAdmission=$true;serverConfirmedGeneration=[string]$reconnect.newGeneration;
    ownedProcessLoss=$true;realClockGrace=$true;snapshotInjection=$false;
    healthChangesVerified=[bool]$vitals;incapacitatedStateVerified=[bool]$vitals;hardwareInputVerified=$false;
    authorizedDamageFixture=[bool]$vitals;naturalCombatVerified=$false;
    fullLifecyclePassed=[bool]$runtime.passed;fullLifecycleFailure=$runtime.failure;
    sourceRuntimeProof=$sourcePath;sourceRuntimeProofSHA256=(Get-FileHash -LiteralPath $sourcePath).Hash;
    moduleSHA256=$runtime.moduleSHA256;recordedInputsStillMatch=$true;
    validatorSHA256=(Get-FileHash -LiteralPath $PSCommandPath).Hash;
    steam=$false;shipping=$false
}
if ($WriteProof) {
    $destination=Join-Path $run 'PartyHudObservation.proof.json'
    if (Test-Path -LiteralPath $destination) { throw 'Evidence already sealed; do not overwrite it.' }
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $destination -Encoding UTF8
}
[pscustomobject]@{passed=$true;hudObservations=$observations.Count;fullLifecyclePassed=[bool]$runtime.passed;run=$run} | ConvertTo-Json -Compress
