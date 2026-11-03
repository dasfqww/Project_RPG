#include "Ability/RPGBossDashAbility.h"

#include "AI/RPGBossDashComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "Engine/World.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffect.h"
#include "GameFramework/RootMotionSource.h"

namespace
{
	bool HasOtherMotion(const UCharacterMovementComponent* Movement, FName OwnedName)
	{
		const auto IsOther = [OwnedName](const TSharedPtr<FRootMotionSource>& Source)
		{
			return Source.IsValid() && Source->InstanceName != OwnedName && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval);
		};
		return Movement->HasAnimRootMotion() || Movement->CurrentRootMotion.RootMotionSources.ContainsByPredicate(IsOther) ||
			Movement->CurrentRootMotion.PendingAddRootMotionSources.ContainsByPredicate(IsOther);
	}
}

bool URPGBossDashAbility::IsMovementReady(const ACharacter* Character) const
{
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return Movement && Movement->IsActive() && Movement->UpdatedComponent && Character->GetActorEnableCollision() &&
		(Movement->MovementMode == MOVE_Walking || Movement->MovementMode == MOVE_NavWalking || Movement->MovementMode == MOVE_Flying);
}

bool URPGBossDashAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const URPGBossAIComponent* BossStateComponent = Character ? Character->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const URPGBossDashComponent* Dash = Character ? Character->FindComponentByClass<URPGBossDashComponent>() : nullptr;
	const URPGBossThreatComponent* TargetSelection = Character ? Character->FindComponentByClass<URPGBossThreatComponent>() : nullptr;
	const APawn* Target = TargetSelection ? TargetSelection->GetCurrentTarget() : nullptr;
	const UGameplayEffect* Effect = DamageEffectClass ? DamageEffectClass->GetDefaultObject<UGameplayEffect>() : nullptr;
	const float Damage = BaseDamage.GetValueAtLevel(GetAbilityLevel());
	FString Error;
	return IsMovementReady(Character) && Character->GetIsReplicated() && Character->IsReplicatingMovement() &&
		BossStateComponent && !BossStateComponent->GetTelegraphState().bActive && Dash && Dash->GetIsReplicated() && !Dash->GetDashState().bActive &&
		!Character->GetCharacterMovement()->HasRootMotionSources() && !Character->GetCharacterMovement()->HasAnimRootMotion() &&
		ActorInfo->MovementComponent.Get() == Character->GetCharacterMovement() && Target &&
		!Target->GetActorLocation().ContainsNaN() && !Character->GetActorLocation().ContainsNaN() &&
		FVector::DistSquared2D(Character->GetActorLocation(), Target->GetActorLocation()) >= 1.f &&
		ActionTag.IsValid() && FMath::IsFinite(WindupSeconds) && WindupSeconds >= .01f && WindupSeconds <= 120.f &&
		FMath::IsFinite(MaximumDistance) && MaximumDistance >= 1.f && MaximumDistance <= 3000.f &&
		FMath::IsFinite(DashDuration) && DashDuration >= .05f && DashDuration <= 5.f && MaximumDistance / DashDuration <= 6000.f &&
		FMath::IsFinite(HitRadius) && HitRadius > 0.f && HitRadius <= 500.f &&
		SecurityProfile.bRequireAuthorityHitQuery && SecurityProfile.IsValid(&Error) &&
		FMath::IsFinite(Damage) && Damage > 0.f && Damage <= SecurityProfile.MaximumDamagePerHit &&
		Effect && Effect->DurationPolicy == EGameplayEffectDurationType::Instant && Effect->Period.GetValueAtLevel(1.f) == 0.f &&
		Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void URPGBossDashAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive() || !IsBossExecutionValid()) { return; }
	const uint64 Serial = GetBossExecutionSerial();
	SourceCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	SourceController = SourceCharacter.IsValid() ? SourceCharacter->GetController() : nullptr;
	AimTarget = GetBossExecutionTarget();
	DashState = SourceCharacter.IsValid() ? SourceCharacter->FindComponentByClass<URPGBossDashComponent>() : nullptr;
	const bool bCommitted = IsMovementReady(SourceCharacter.Get()) && DashState.IsValid() && AimTarget.IsValid() && CommitAbility(Handle, ActorInfo, ActivationInfo);
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	if (!bCommitted || !IsBossExecutionValid()) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	FrozenDuration = DashDuration;
	FrozenRadius = HitRadius;
	FrozenDistanceLimit = MaximumDistance;
	FrozenSecurity = SecurityProfile;
	const FVector Start = SourceCharacter->GetActorLocation();
	const FVector Delta = AimTarget->GetActorLocation() - Start;
	LockedAim = Start + Delta.GetSafeNormal2D() * FMath::Min(Delta.Size2D(), MaximumDistance);
	LockedEncounter = GetBossExecutionEncounterId();
	HitParticipants.Reset();
	DamageTemplate = DamageEffectClass ? MakeEnemyDamageEffectSpecHandle(DamageEffectClass, BaseDamage) : FGameplayEffectSpecHandle();
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	if (!DamageTemplate.IsValid() || !DamageTemplate.Data->Def ||
		DamageTemplate.Data->Def->DurationPolicy != EGameplayEffectDurationType::Instant || DamageTemplate.Data->GetPeriod() != 0.f ||
		!IsBossExecutionValid()) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	SourceCharacter->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnControllerChanged);
	AimTarget->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnControllerChanged);
	SourceCharacter->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnSourceDestroyed);
	if (AAIController* AI = Cast<AAIController>(SourceController.Get())) { AI->StopMovement(); }
	if (!IsDashContextValid(Serial, true)) { if (IsActive() && GetBossExecutionSerial() == Serial) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); } return; }
	SourceCharacter->GetCharacterMovement()->StopMovementImmediately();
	FRPGHitQueryProfile WarningProfile;
	const FVector WarningDelta = LockedAim - Start;
	WarningProfile.Shape.Type = WarningDelta.IsNearlyZero() ? ERPGHitQueryShape::Sphere : ERPGHitQueryShape::LineSweep;
	WarningProfile.Shape.Radius = FrozenRadius;
	WarningProfile.Shape.Length = WarningDelta.Size();
	const FTransform WarningTransform(WarningDelta.GetSafeNormal2D().Rotation(), Start);
	const int32 Sequence = StartBossTelegraphForHitQuery(ActionTag, WarningTransform, WarningProfile, WindupSeconds);
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	LockedSequence = Sequence;
	if (Sequence == 0) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	GetWorld()->GetTimerManager().SetTimer(CastTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Serial]() { ReleaseDash(Serial); }), WindupSeconds, false);
}

bool URPGBossDashAbility::IsDashContextValid(uint64 Serial, bool bRequireAim) const
{
	if (!IsActive() || GetBossExecutionSerial() != Serial || !SourceCharacter.IsValid() || !SourceCharacter->HasAuthority() ||
		!SourceController.IsValid() || SourceCharacter->GetController() != SourceController.Get() || !DashState.IsValid() ||
		GetAvatarActorFromActorInfo() != SourceCharacter.Get() || !GetAbilitySystemComponentFromActorInfo() ||
		GetAbilitySystemComponentFromActorInfo()->GetAvatarActor() != SourceCharacter.Get() ||
		!CurrentActorInfo || CurrentActorInfo->MovementComponent.Get() != SourceCharacter->GetCharacterMovement() ||
		!DamageTemplate.IsValid() || DamageTemplate.Data->GetContext().GetInstigator() != SourceCharacter.Get() ||
		DamageTemplate.Data->GetContext().GetInstigatorAbilitySystemComponent() != GetAbilitySystemComponentFromActorInfo()) { return false; }
	const URPGBossAIComponent* BossStateComponent = SourceCharacter->FindComponentByClass<URPGBossAIComponent>();
	const FRPGBossState Context = BossStateComponent ? BossStateComponent->GetBossState() : FRPGBossState();
	if (Context.EncounterState != ERPGBossEncounterState::InCombat || Context.EncounterId != LockedEncounter) { return false; }
	if (bDashing)
	{
		const FRPGBossDashState Snapshot = DashState->GetDashState();
		if (!Snapshot.bActive || Snapshot.EncounterId != LockedEncounter || Snapshot.Sequence != LockedSequence || Context.PhaseIndex != Snapshot.PhaseIndex) { return false; }
	}
	return !bRequireAim || IsBossExecutionValid();
}

void URPGBossDashAbility::ReleaseDash(uint64 Serial)
{
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	const URPGBossAIComponent* BossStateComponent = SourceCharacter.IsValid() ? SourceCharacter->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const FRPGBossTelegraphState Telegraph = BossStateComponent ? BossStateComponent->GetTelegraphState() : FRPGBossTelegraphState();
	if (!IsDashContextValid(Serial, true) || !IsMovementReady(SourceCharacter.Get()) || !Telegraph.bActive ||
		Telegraph.EncounterId != LockedEncounter || Telegraph.Sequence != LockedSequence || !FinishBossTelegraph())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return;
	}
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	if (!IsDashContextValid(Serial, true)) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	LastLocation = SourceCharacter->GetActorLocation();
	const FVector Direction = (LockedAim - LastLocation).GetSafeNormal2D();
	const float Distance = FVector::Dist2D(LastLocation, LockedAim);
	const AGameStateBase* Clock = GetWorld()->GetGameState();
	FRPGBossDashState Snapshot;
	Snapshot.bActive = true;
	Snapshot.EncounterId = LockedEncounter;
	Snapshot.PhaseIndex = BossStateComponent->GetBossState().PhaseIndex;
	Snapshot.Sequence = LockedSequence;
	Snapshot.ActionTag = Telegraph.ActionTag;
	Snapshot.StartLocation = LastLocation;
	Snapshot.EndLocation = LastLocation + Direction * Distance;
	Snapshot.HitRadius = FrozenRadius;
	Snapshot.StartServerTime = Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	Snapshot.EndServerTime = Snapshot.StartServerTime + FrozenDuration;
	FrozenSpeed = Distance / FrozenDuration;
	if (Distance < 1.f || Distance > FrozenDistanceLimit + 10.f || Distance > 3000.f || FrozenSpeed > 6000.f || !DashState->BeginDash(Snapshot))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return;
	}
	bDashing = true;
	SourceCharacter->OnCharacterMovementUpdated.AddUniqueDynamic(this, &ThisClass::OnMovementUpdated);
	// Do not change movement mode, collision responses, or other root-motion sources.
	MotionName = FName(*FString::Printf(TEXT("%s_Dash_%llu"), *GetName(), Serial));
	MotionTask = UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(this, MotionName, Direction,
		FrozenSpeed, FrozenDuration, false, nullptr, ERootMotionFinishVelocityMode::SetVelocity, FVector::ZeroVector, 0.f, true);
	if (!MotionTask) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	MotionTask->OnFinish.AddDynamic(this, &ThisClass::OnMotionFinished);
	MotionTask->ReadyForActivation();
	if (IsActive() && GetBossExecutionSerial() == Serial)
	{
		// Also bound the action when an externally paused task stops ticking.
		GetWorld()->GetTimerManager().SetTimer(CastTimer, FTimerDelegate::CreateWeakLambda(this, [this, Serial]()
		{
			if (IsActive() && GetBossExecutionSerial() == Serial) { OnMotionFinished(); }
		}), FrozenDuration, false);
	}
}

void URPGBossDashAbility::OnMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!bDashing || bResolvingHits) { return; }
	const uint64 Serial = GetBossExecutionSerial();
	if (!IsDashContextValid(Serial, true) || !IsMovementReady(SourceCharacter.Get()))
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return;
	}
	const FVector Location = SourceCharacter->GetActorLocation();
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f || OldLocation.ContainsNaN() || Location.ContainsNaN() ||
		FVector::DistSquared(LastLocation, OldLocation) > FMath::Square(10.f) ||
		FVector::Distance(OldLocation, Location) > FrozenSpeed * DeltaSeconds + 25.f)
	{
		// A teleport/correction is not a traveled attack path.
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return;
	}
	LastLocation = Location;
	ResolveSegment(OldLocation, Location, Serial);
}

void URPGBossDashAbility::ResolveSegment(FVector Start, FVector End, uint64 Serial)
{
	if (!IsDashContextValid(Serial, true) || HitParticipants.Num() >= FrozenSecurity.MaximumHitsPerActivation) { return; }
	TGuardValue<bool> ResolvingGuard(bResolvingHits, true);
	URPGHitQuerySubsystem* Queries = GetWorld()->GetSubsystem<URPGHitQuerySubsystem>();
	URPGBossThreatComponent* TargetSelection = SourceCharacter->FindComponentByClass<URPGBossThreatComponent>();
	if (!Queries || !TargetSelection) { return; }
	FRPGHitQueryContext Query;
	Query.SourceActor = SourceCharacter.Get();
	const FVector Segment = End - Start;
	Query.QueryTransform = FTransform(Segment.IsNearlyZero() ? FQuat::Identity : Segment.Rotation().Quaternion(), Start);
	Query.Profile.Shape.Type = Segment.IsNearlyZero() ? ERPGHitQueryShape::Sphere : ERPGHitQueryShape::LineSweep;
	Query.Profile.Shape.Length = Segment.Size();
	Query.Profile.Shape.Radius = FrozenRadius;
	Query.Profile.Filter.TeamRule = ERPGHitQueryTeamRule::HostileOnly;
	Query.Profile.Filter.bRequireLineOfSight = true;
	Query.Profile.Filter.MaxResults = FrozenSecurity.MaximumTargetsPerQuery;
	for (const TWeakObjectPtr<APawn>& Pawn : HitParticipants) { if (Pawn.IsValid()) { Query.AlreadyHitActors.Add(Pawn.Get()); } }
	TArray<FRPGHitQueryResult> Hits;
	Queries->ExecuteHitQuery(Query, Hits);
	if (!IsDashContextValid(Serial, true)) { return; }
	TArray<TWeakObjectPtr<AController>> Controllers;
	for (const FRPGHitQueryResult& Hit : Hits)
	{
		APawn* Pawn = Cast<APawn>(Hit.TargetActor);
		Controllers.Add(Pawn && TargetSelection->GetThreatForTarget(Pawn) > 0.0 ? Pawn->GetController() : nullptr);
	}
	const FGameplayEffectSpecHandle Template = DamageTemplate;
	for (int32 Index = 0; Index < Hits.Num(); ++Index)
	{
		// A killed aim does not spare the other participants already resolved in this segment.
		// The next movement update/BT tick still cancels a lost target as usual.
		if (!IsDashContextValid(Serial, false) || HitParticipants.Num() >= FrozenSecurity.MaximumHitsPerActivation) { break; }
		APawn* Pawn = Cast<APawn>(Hits[Index].TargetActor);
		if (!IsValid(Pawn) || Hits[Index].HitResult.GetActor() != Pawn || !Controllers[Index].IsValid() ||
			Pawn->GetController() != Controllers[Index].Get() || HitParticipants.Contains(Pawn) || TargetSelection->GetThreatForTarget(Pawn) <= 0.0) { continue; }
		HitParticipants.Add(Pawn); // Consume before synchronous GE callbacks; no reentrant duplicate.
		const FGameplayEffectSpecHandle Spec(new FGameplayEffectSpec(*Template.Data));
		URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(SourceCharacter.Get(), Hits[Index].HitResult, Spec, FrozenSecurity);
	}
}

void URPGBossDashAbility::OnMotionFinished()
{
	if (bDashing && IsActive()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !IsBossExecutionValid()); }
}

void URPGBossDashAbility::OnControllerChanged(APawn* Pawn, AController* Previous, AController* Current)
{
	if (IsActive() && Previous != Current) { CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true); }
}

void URPGBossDashAbility::OnSourceDestroyed(AActor* Actor)
{
	if (IsActive()) { CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true); }
}

void URPGBossDashAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) { return; }
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CastTimer); }
	bDashing = false;
	if (SourceCharacter.IsValid())
	{
		SourceCharacter->OnCharacterMovementUpdated.RemoveDynamic(this, &ThisClass::OnMovementUpdated);
		SourceCharacter->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnControllerChanged);
		SourceCharacter->OnDestroyed.RemoveDynamic(this, &ThisClass::OnSourceDestroyed);
	}
	if (AimTarget.IsValid()) { AimTarget->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnControllerChanged); }
	UAbilityTask_ApplyRootMotionConstantForce* PreviousTask = MotionTask;
	MotionTask = nullptr;
	if (PreviousTask)
	{
		PreviousTask->OnFinish.RemoveDynamic(this, &ThisClass::OnMotionFinished);
		PreviousTask->EndTask();
		// Preserve a disabled mode or replacement root motion owned by another action.
		if (SourceCharacter.IsValid() && (!SourceCharacter->GetController() || SourceCharacter->GetController() == SourceController.Get()) &&
			!HasOtherMotion(SourceCharacter->GetCharacterMovement(), MotionName)) { SourceCharacter->GetCharacterMovement()->StopMovementImmediately(); }
	}
	if (DashState.IsValid()) { DashState->EndDash(LockedEncounter, LockedSequence); }
	DamageTemplate = FGameplayEffectSpecHandle();
	HitParticipants.Reset();
	LockedSequence = 0;
	LockedEncounter = 0;
	DashState.Reset();
	AimTarget.Reset();
	SourceCharacter.Reset();
	SourceController.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
