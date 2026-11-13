[CmdletBinding()]
param([string]$EngineRoot='D:/UE_5.8', [switch]$IncludePresentation, [switch]$IncludeWorld, [switch]$IncludeAbilities, [switch]$IncludeConsume, [switch]$IncludeDamage, [switch]$IncludeControl, [switch]$IncludeDashAdmission, [switch]$IncludeDashMotion, [switch]$IncludeDashGAS, [switch]$IncludeDashReplay, [switch]$IncludeDashGrant, [switch]$IncludeDashPrediction, [switch]$IncludeEmote, [switch]$IncludeInteraction, [switch]$IncludeLocalInteraction, [switch]$IncludeTimedInteraction, [switch]$IncludeSnapshot, [switch]$IncludeCollection, [switch]$EmotePresentationOnly, [ValidateRange(1,4)][int]$MaxParallelActions=1)
$ErrorActionPreference='Stop'
if ($EmotePresentationOnly -and !$IncludeEmote) { throw 'EmotePresentationOnly requires IncludeEmote' }
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$run=[Guid]::NewGuid().ToString('N')
if ($IncludeControl) { $IncludeDamage = $true }
if ($IncludeEmote) { $IncludeDashPrediction = $true }
$proofFolder = if ($IncludeDashMotion) { 'RidingDashMotion-20261109' } elseif ($IncludeDashAdmission) { 'RidingDashAdmission-20261109' } elseif ($IncludeControl) { 'RidingControl-20261109' } elseif ($IncludeDamage) { 'RidingDamage-20261109' } elseif ($IncludeConsume) { 'RidingConsume-20261108' } elseif ($IncludeAbilities) { 'RidingAbilityGate-20261108' } elseif ($IncludeWorld) { 'RidingRegions-20261108' } elseif ($IncludePresentation) { 'RidingPresentation-20261108' } else { 'RidingActors-20261106' }
$reservation = if ($IncludeDashMotion) { 'riding-dash-motion-20261109' } elseif ($IncludeDashAdmission) { 'riding-dash-admission-20261109' } elseif ($IncludeControl) { 'riding-control-20261109' } elseif ($IncludeDamage) { 'riding-damage-20261109' } elseif ($IncludeConsume) { 'riding-consume-20261108' } elseif ($IncludeAbilities) { 'riding-ability-gate-20261108' } elseif ($IncludeWorld) { 'riding-regions-20261108' } elseif ($IncludePresentation) { 'riding-presentation-20261108' } else { 'riding-actors-20261106' }
$proofRoot=Join-Path $workspace ('Saved/BuildChecks/'+$proofFolder+'/Integration-'+$run)
$tempRoot=Join-Path $workspace 'Saved/RF'
if ($IncludeDashGAS) {
    $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingDashGAS-20261109/Integration-'+$run)
    $reservation='riding-dash-gas-20261109'
}
$temp=Join-Path $tempRoot $run.Substring(0,8)
if ($IncludeDashReplay) {
    $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingDashReplay-20261110/Integration-'+$run)
    $reservation='riding-dash-replay-20261110'
}
if ($IncludeDashPrediction) {
    $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingDashPrediction-20261110/Integration-'+$run)
    $reservation='riding-dash-prediction-20261110'
}
if ($IncludeSnapshot) { $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingUEAdmission-20261113/Integration-'+$run); $reservation='riding-ue-admission-20261113' }
$project=Join-Path $temp 'Project'
if ($IncludeDashGrant) {
    $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingDashGrant-20261110/Integration-'+$run)
    $reservation='riding-dash-grant-20261110'
}
$proof=[ordered]@{Passed=$false;Scope='Full published project UHT and focused integration CPP compile, no module link/runtime';Head='';Inputs=@();Cleanup=$null;Failure=$null}
if ($IncludeEmote) {
    $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingEmote-20261112/Integration-'+$run)
    $reservation='riding-emote-20261112'
}
if ($IncludeInteraction) { $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingInteraction-20261112/Integration-'+$run); $reservation='riding-interaction-20261112' }
if ($IncludeLocalInteraction) { $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingLocalInteraction-20261113/Integration-'+$run); $reservation='riding-local-interaction-20261113' }
if ($IncludeTimedInteraction) { $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingTimedInteraction-20261113/Integration-'+$run); $reservation='riding-timed-interaction-20261113' }
if ($IncludeCollection) { $proofRoot=Join-Path $workspace ('Saved/BuildChecks/RidingOwnerRead-20261114/Integration-'+$run); $reservation='riding-owner-read-20261114' }
. (Join-Path $workspace 'Scripts/Network/GuardianMelee2Artifacts.ps1')
function Save-Proof { [IO.File]::WriteAllText((Join-Path $proofRoot 'Result.json'),($proof|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false)) }
function Assert-Reservation {
    if (@(Get-Content -LiteralPath (Join-Path $workspace 'Docs/Online_RPG_Development_Roadmap.md') -Encoding UTF8 | Where-Object {$_ -match ('^\|[^|]* / '+[regex]::Escape($reservation)+' \|')}).Count -ne 1) { throw 'Riding reservation missing' }
}
try {
    Assert-Reservation
    $null=[IO.Directory]::CreateDirectory($project)
    $null=[IO.Directory]::CreateDirectory($proofRoot)
    $proof.Head=(git -C $workspace rev-parse HEAD).Trim()
    $paths=@('Source','Config','Project_RPG.uproject')
    $pluginFiles=@(git -C $workspace ls-tree -r --name-only HEAD -- Plugins)
    foreach($descriptor in @($pluginFiles|Where-Object {$_ -like '*.uplugin'})) {
        $paths+=$descriptor
        $dir=$descriptor.Substring(0,$descriptor.LastIndexOf('/'))
        foreach($name in @('Source','Config','Resources','Shaders')) {
            $child=$dir+'/'+$name
            if (@($pluginFiles|Where-Object {$_.StartsWith($child+'/')}).Count) {$paths+=$child}
        }
    }
    $archive=Join-Path $temp 'Inputs.tar'
    & git -C $workspace archive --format=tar ('-o'+$archive) $proof.Head -- $paths
    if($LASTEXITCODE -ne 0){throw 'Source archive failed'}
    & tar -xf $archive -C $project
    if($LASTEXITCODE -ne 0){throw 'Source extraction failed'}
    $files=@('Source/Project_RPG/Public/Riding/RPGRidingTypes.h',
        'Source/Project_RPG/Private/Character/RPGPlayer.cpp',
        'Source/Project_RPG/Public/Player/RPGPlayerState.h','Source/Project_RPG/Private/Player/RPGPlayerState.cpp',
        'Source/Project_RPG/Public/GameMode/RPGGameModeBase.h','Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp',
        'Source/Project_RPG/Private/GameMode/RPGGameModeRiding.cpp')
    foreach($name in @('RPGRidingSessionComponent','RPGRidingComponent')) {
        $files+='Source/Project_RPG/Public/Riding/'+$name+'.h'
        $files+='Source/Project_RPG/Private/Riding/'+$name+'.cpp'
    }
    if ($IncludePresentation) {
        foreach($name in @('RPGMountDefinition','RPGRidingPresentationComponent')) {
            $files+='Source/Project_RPG/Public/Riding/'+$name+'.h'
            $files+='Source/Project_RPG/Private/Riding/'+$name+'.cpp'
        }
    }
    if ($IncludeWorld) {
        $files=@()
        foreach($name in @('RPGRidingComponent','RPGRidingWorldPolicy','RPGRidingRegionVolume')) {
            $files+='Source/Project_RPG/Public/Riding/'+$name+'.h'
            $files+='Source/Project_RPG/Private/Riding/'+$name+'.cpp'
        }
        $files+=@('Source/Project_RPG/Public/Component/RPGPlayerMovementComponent.h','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingActorTests.cpp')
    }
    if ($IncludeAbilities) {
        $files=@('Source/Project_RPG/Public/Riding/RPGRidingAbilityGate.h','Source/Project_RPG/Private/Riding/RPGRidingAbilityGate.cpp',
            'Source/Project_RPG/Public/Riding/RPGRidingAbilityPolicy.h','Source/Project_RPG/Public/Ability/RPGGameplayAbility.h',
            'Source/Project_RPG/Private/Ability/RPGGameplayAbility.cpp','Source/Project_RPG/Public/Component/RPGAbilitySystemComponent.h',
            'Source/Project_RPG/Private/Component/RPGAbilitySystemComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp')
    }
    if ($IncludeConsume) {
        $files=@('Source/Project_RPG/Public/Riding/RPGRidingConsumeLease.h','Source/Project_RPG/Private/Riding/RPGRidingConsumeLease.cpp',
            'Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp')
    }
    if ($IncludeDamage) {
        $files=@('Source/Project_RPG/Public/Riding/RPGRidingDamageScope.h','Source/Project_RPG/Private/Riding/RPGRidingDamageScope.cpp',
            'Source/Project_RPG/Public/Riding/RPGRidingSessionComponent.h','Source/Project_RPG/Private/Riding/RPGRidingSessionComponent.cpp',
            'Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp')
    }
    $controlFiles=@('Source/Project_RPG/Private/Component/Combat/RPGCombatControlComponent.cpp','Source/Project_RPG/Private/Component/Combat/RPGCombatStagger.cpp','Source/Project_RPG/Private/Component/Combat/RPGCombatSlow.cpp','Source/Project_RPG/Private/Component/Combat/RPGCombatKnockback.cpp','Source/Project_RPG/Private/Component/Combat/RPGCombatAirborne.cpp')
    if ($IncludeControl) { $files += $controlFiles }
    if ($IncludeDashAdmission) { $files=@('Source/Project_RPG/Public/Riding/RPGRidingDashAdmission.h','Source/Project_RPG/Public/Riding/RPGRidingComponent.h','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Public/Riding/RPGRidingSessionComponent.h','Source/Project_RPG/Private/Riding/RPGRidingSession.h','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp') }
    if ($IncludeDashMotion) { $files=@('Source/Project_RPG/Public/Riding/RPGRidingDashMotionHost.h','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.h','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.cpp','Source/Project_RPG/Public/Riding/RPGRidingComponent.h','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Public/Component/RPGPlayerMovementComponent.h','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Component/Combat/RPGCombatKnockback.cpp','Source/Project_RPG/Private/Component/Combat/RPGCombatAirborne.cpp') }
    if ($IncludeDashGAS) { $files=@('Source/Project_RPG/Public/Riding/RPGGameplayAbility_RidingDash.h','Source/Project_RPG/Private/Riding/RPGGameplayAbility_RidingDash.cpp','Source/Project_RPG/Public/Riding/RPGRidingComponent.h','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashAbilityTests.cpp') }
    if ($IncludeDashReplay) { $files=@('Source/Project_RPG/Public/Riding/RPGRidingDashRootMotion.h','Source/Project_RPG/Private/Riding/RPGRidingDashRootMotion.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.cpp','Source/Project_RPG/Private/Riding/RPGRidingMovementPrediction.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashReplayTests.cpp') }
    if ($IncludeDashGrant) { $files=@('Source/Project_RPG/Public/Riding/RPGRidingDashGrant.h','Source/Project_RPG/Private/Riding/RPGRidingDashGrant.cpp','Source/Project_RPG/Public/Riding/RPGRidingDashAdmission.h','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp','Source/Project_RPG/Public/Riding/RPGRidingComponent.h','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Riding/RPGGameplayAbility_RidingDash.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.h','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.cpp','Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashGrantTests.cpp') }
    if ($IncludeDashPrediction) { $files=@('Source/Project_RPG/Public/Riding/RPGRidingDashIntent.h','Source/Project_RPG/Private/Riding/RPGRidingDashIntent.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashGeometry.h','Source/Project_RPG/Private/Riding/RPGRidingDashGeometry.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashPrediction.h','Source/Project_RPG/Private/Riding/RPGRidingDashPrediction.cpp','Source/Project_RPG/Public/Riding/RPGRidingComponent.h','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Public/Riding/RPGGameplayAbility_RidingDash.h','Source/Project_RPG/Private/Riding/RPGGameplayAbility_RidingDash.cpp','Source/Project_RPG/Public/Riding/RPGRidingMovementPrediction.h','Source/Project_RPG/Private/Riding/RPGRidingMovementPrediction.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.cpp','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Component/RPGAbilitySystemTargetDataSecurity.cpp','Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashMotionTests.h','Source/Project_RPG/Private/Tests/RPGRidingDashMotionTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashGrantTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashPredictionTests.h','Source/Project_RPG/Private/Tests/RPGRidingDashPredictionTests.cpp') }
    if ($IncludeEmote) {
        $files+=@('Source/Project_RPG/Public/Riding/RPGRidingEmoteAdmission.h','Source/Project_RPG/Public/Riding/RPGGameplayAbility_RidingEmote.h','Source/Project_RPG/Private/Riding/RPGGameplayAbility_RidingEmote.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponentEmote.cpp','Source/Project_RPG/Private/Riding/RPGRidingSession.h','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp','Source/Project_RPG/Public/Riding/RPGRidingSessionComponent.h','Source/Project_RPG/Public/Riding/RPGRidingPresentationComponent.h','Source/Project_RPG/Private/Riding/RPGRidingPresentationComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingEmoteTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingPresentationTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingActionBarTests.cpp')
    }
    if ($IncludeInteraction) { $files+=@('Source/Project_RPG/Public/Riding/RPGRidingInteractionScope.h','Source/Project_RPG/Private/Riding/RPGRidingInteractionScope.cpp','Source/Project_RPG/Public/Riding/RPGRidingComponent.h','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp','Source/Project_RPG/Private/Narrative/RPGNarrativeInvestigationComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingInteractionTests.cpp') }
    if ($IncludeLocalInteraction) { $files+=@('Source/Project_RPG/Public/Riding/RPGRidingLocalInteractionComponent.h','Source/Project_RPG/Private/Riding/RPGRidingLocalInteractionComponent.cpp','Source/Project_RPG/Public/Interface/InteractionInterface.h','Source/Project_RPG/Private/Interface/InteractionInterface.cpp','Source/Project_RPG/Public/Actor/Narrative/RPGNarrativeNPC.h','Source/Project_RPG/Private/Actor/Narrative/RPGNarrativeNPC.cpp','Source/Project_RPG/Public/Actor/Content/RPGContentEntryActor.h','Source/Project_RPG/Private/Actor/Content/RPGContentEntryActor.cpp','Source/Project_RPG/Public/Controller/RPGPlayerController.h','Source/Project_RPG/Private/Controller/RPGPlayerController.cpp','Source/Project_RPG/Private/Tests/RPGRidingLocalInteractionTests.h','Source/Project_RPG/Private/Tests/RPGRidingLocalInteractionTests.cpp') }
    if ($IncludeTimedInteraction) { $files+=@('Source/Project_RPG/Public/Riding/RPGRidingTimedInteractionComponent.h','Source/Project_RPG/Private/Riding/RPGRidingTimedInteractionComponent.cpp','Source/Project_RPG/Public/Riding/RPGRidingTimedInteractionLease.h','Source/Project_RPG/Private/Riding/RPGRidingTimedInteractionLease.cpp','Source/Project_RPG/Private/Tests/RPGRidingTimedInteractionTests.h','Source/Project_RPG/Private/Tests/RPGRidingTimedInteractionTests.cpp','Source/Project_RPG/Public/Interface/InteractionInterface.h','Source/Project_RPG/Public/Item/PickUp/RPGPickUpBase.h','Source/Project_RPG/Private/Item/PickUp/RPGPickUpBase.cpp','Source/Project_RPG/Public/Component/RPGItemCommandComponent.h','Source/Project_RPG/Public/Controller/RPGPlayerController.h','Source/Project_RPG/Private/Controller/RPGPlayerController.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp') }
    $files+=@('Source/Project_RPG/Private/Riding/RPGRidingSnapshot.h','Source/Project_RPG/Private/Riding/RPGRidingSnapshot.cpp','Source/Project_RPG/Private/Riding/RPGRidingSnapshotHost.cpp','Source/Project_RPG/Private/Riding/RPGRidingSession.h','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp')
    if ($IncludeSnapshot) { $files+=@('Source/Project_RPG/Public/Riding/RPGRidingSessionComponent.h','Source/Project_RPG/Private/Riding/RPGRidingSessionComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Player/RPGPlayerState.cpp','Source/Project_RPG/Private/Tests/RPGRidingSnapshotTests.cpp') }
    if ($IncludeCollection) { $files+=@('Source/Project_RPG/Public/Riding/RPGRidingCollection.h','Source/Project_RPG/Private/Riding/RPGRidingCollection.cpp','Source/Project_RPG/Public/Riding/RPGRidingOwnerTransport.h','Source/Project_RPG/Private/Riding/RPGRidingOwnerTransport.cpp','Source/Project_RPG/Public/Riding/RPGRidingCollectionReader.h','Source/Project_RPG/Private/Riding/RPGRidingCollectionReader.cpp','Source/Project_RPG/Public/Manager/HttpWebManager.h','Source/Project_RPG/Private/Manager/HttpWebManagerRiding.cpp','Source/Project_RPG/Private/Tests/RPGRidingCollectionTests.cpp') }
    foreach($relative in @($files|Select-Object -Unique)) {
        $source=Join-Path $workspace $relative
        $destination=Join-Path $project $relative
        $sha=(Get-FileHash -LiteralPath $source).Hash
        $null=[IO.Directory]::CreateDirectory((Split-Path $destination))
        Copy-Item -LiteralPath $source -Destination $destination
        [IO.File]::SetLastWriteTimeUtc($destination,[DateTime]::UtcNow)
        if((Get-FileHash -LiteralPath $destination).Hash -ne $sha -or (Get-FileHash -LiteralPath $source).Hash -ne $sha){throw 'Overlay changed during capture'}
        $proof.Inputs+=@{Path=$relative;SHA256=$sha}
    }
    $fileList=Join-Path $temp 'CompileFiles.txt'
    $compile=@('Source/Project_RPG/Private/Character/RPGPlayer.cpp','Source/Project_RPG/Private/Player/RPGPlayerState.cpp',
        'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp','Source/Project_RPG/Private/GameMode/RPGGameModeRiding.cpp')
    if ($IncludePresentation) { $compile=@('Source/Project_RPG/Private/Character/RPGPlayer.cpp') }
    if ($IncludeWorld) { $compile=@('Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingWorldPolicy.cpp','Source/Project_RPG/Private/Riding/RPGRidingRegionVolume.cpp') }
    if ($IncludeAbilities) { $compile=@('Source/Project_RPG/Private/Component/RPGAbilitySystemComponent.cpp','Source/Project_RPG/Private/Ability/RPGGameplayAbility.cpp','Source/Project_RPG/Private/Riding/RPGRidingAbilityGate.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp') }
    if ($IncludeConsume) { $compile=@('Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingConsumeLease.cpp') }
    if ($IncludeDamage) { $compile=@('Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingSessionComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingDamageScope.cpp') }
    if ($IncludeControl) { $compile=$controlFiles+@('Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp','Source/Project_RPG/Private/Riding/RPGRidingDamageScope.cpp') }
    if ($IncludeDashAdmission) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp') }
    if ($IncludeDashMotion) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Component/Combat/RPGCombatKnockback.cpp','Source/Project_RPG/Private/Component/Combat/RPGCombatAirborne.cpp') }
    if ($IncludeDashGAS) { $compile=@('Source/Project_RPG/Private/Riding/RPGGameplayAbility_RidingDash.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashAbilityTests.cpp') }
    if ($IncludeDashReplay) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingDashRootMotion.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.cpp','Source/Project_RPG/Private/Riding/RPGRidingMovementPrediction.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashReplayTests.cpp') }
    if ($IncludeDashGrant) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingDashGrant.cpp','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Riding/RPGGameplayAbility_RidingDash.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.cpp','Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashGrantTests.cpp') }
    if ($IncludeDashPrediction) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingDashIntent.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashGeometry.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashPrediction.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Riding/RPGGameplayAbility_RidingDash.cpp','Source/Project_RPG/Private/Riding/RPGRidingMovementPrediction.cpp','Source/Project_RPG/Private/Riding/RPGRidingDashRuntime.cpp','Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp','Source/Project_RPG/Private/Component/RPGAbilitySystemTargetDataSecurity.cpp','Source/Project_RPG/Private/UI/ActionBar/RPGRidingActionBarSubsystem.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashMotionTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashGrantTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingDashPredictionTests.cpp') }
    if ($IncludeEmote) {
        $compile+=@('Source/Project_RPG/Private/Riding/RPGRidingComponentEmote.cpp','Source/Project_RPG/Private/Riding/RPGGameplayAbility_RidingEmote.cpp','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp','Source/Project_RPG/Private/Riding/RPGRidingPresentationComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingEmoteTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingPresentationTests.cpp','Source/Project_RPG/Private/Tests/RPGRidingActionBarTests.cpp')
    }
    if ($EmotePresentationOnly) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingPresentationComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingPresentationTests.cpp') }
    if ($IncludeInteraction) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingInteractionScope.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Component/RPGItemCommandComponent.cpp','Source/Project_RPG/Private/Narrative/RPGNarrativeInvestigationComponent.cpp','Source/Project_RPG/Private/Tests/RPGRidingInteractionTests.cpp') }
    if ($IncludeLocalInteraction) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingLocalInteractionComponent.cpp','Source/Project_RPG/Private/Interface/InteractionInterface.cpp','Source/Project_RPG/Private/Actor/Narrative/RPGNarrativeNPC.cpp','Source/Project_RPG/Private/Actor/Content/RPGContentEntryActor.cpp','Source/Project_RPG/Private/Controller/RPGPlayerController.cpp','Source/Project_RPG/Private/Tests/RPGRidingLocalInteractionTests.cpp') }
    if ($IncludeTimedInteraction) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingTimedInteractionComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingTimedInteractionLease.cpp','Source/Project_RPG/Private/Tests/RPGRidingTimedInteractionTests.cpp','Source/Project_RPG/Private/Item/PickUp/RPGPickUpBase.cpp','Source/Project_RPG/Private/Controller/RPGPlayerController.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Actor/Narrative/RPGNarrativeNPC.cpp','Source/Project_RPG/Private/Actor/Content/RPGContentEntryActor.cpp') }
    if ($IncludeSnapshot) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingSnapshot.cpp','Source/Project_RPG/Private/Riding/RPGRidingSnapshotHost.cpp','Source/Project_RPG/Private/Riding/RPGRidingSession.cpp','Source/Project_RPG/Private/Riding/RPGRidingSessionComponent.cpp','Source/Project_RPG/Private/Riding/RPGRidingComponent.cpp','Source/Project_RPG/Private/Player/RPGPlayerState.cpp','Source/Project_RPG/Private/Tests/RPGRidingSnapshotTests.cpp') }
    if ($IncludeCollection) { $compile=@('Source/Project_RPG/Private/Riding/RPGRidingCollection.cpp','Source/Project_RPG/Private/Riding/RPGRidingOwnerTransport.cpp','Source/Project_RPG/Private/Riding/RPGRidingCollectionReader.cpp','Source/Project_RPG/Private/Manager/HttpWebManagerRiding.cpp','Source/Project_RPG/Private/Manager/HttpWebManager.cpp','Source/Project_RPG/Private/Tests/RPGRidingCollectionTests.cpp') }
    $proof.CompileFiles=@($compile|Select-Object -Unique)
    [IO.File]::WriteAllLines($fileList,@($compile|Select-Object -Unique|ForEach-Object {Join-Path $project $_}),[Text.UTF8Encoding]::new($false))
    $dotnet=Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
    $ubt=Join-Path $EngineRoot 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll'
    $args=@($ubt,'Project_RPGEditor','Win64','Development',(Join-Path $project 'Project_RPG.uproject'),
        ('-FileList='+$fileList),'-NoMutex','-NoEngineChanges','-NoUBA','-UBADisableRemote',('-UBARootDir='+(Join-Path $temp 'Uba')),
        '-NoHotReloadFromIDE',('-MaxParallelActions='+$MaxParallelActions),('-log='+(Join-Path $proofRoot 'Build.log')))
    $proof.Command=@($dotnet)+$args
    Save-Proof
    Write-Output ('Compiling actual Riding integration hooks; proof: '+$proofRoot)
    & $dotnet @args *> (Join-Path $proofRoot 'BuildConsole.log')
    $proof.BuildExit=$LASTEXITCODE
    if($LASTEXITCODE -ne 0){throw 'Integration compile failed'}
    foreach($inputFile in $proof.Inputs) {
        if((Get-FileHash -LiteralPath (Join-Path $workspace $inputFile.Path)).Hash -ne $inputFile.SHA256){throw 'Original integration input changed'}
    }
    $proof.Passed=$true
} catch { $proof.Failure=$_.Exception.Message }
finally {
    try {
        Assert-Reservation
        if(Test-Path -LiteralPath $temp){$proof.Cleanup=Remove-GuardianProbeArtifact $temp $tempRoot}
    } catch {$proof.Failure=$_.Exception.Message}
    $proof.CleanupComplete=!(Test-Path -LiteralPath $temp)
    $proof.Passed=$proof.Passed -and !$proof.Failure -and $proof.CleanupComplete
    $proof.HistoricalHashesOnly=$true
    Save-Proof
    Write-Output ('Passed='+$proof.Passed+'; proof='+$proofRoot)
}
if(!$proof.Passed){throw $proof.Failure}
