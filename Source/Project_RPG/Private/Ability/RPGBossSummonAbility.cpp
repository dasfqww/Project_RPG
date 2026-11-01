#include "Ability/RPGBossSummonAbility.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "AIController.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

bool URPGBossSummonAbility::MakePlan(const APawn* Source, FRPGBossSummonPlan& OutPlan) const
{
	OutPlan = FRPGBossSummonPlan();
	if (!IsValid(Source) || Source->GetActorLocation().ContainsNaN() || Source->GetActorRotation().ContainsNaN() ||
		LocalSpawnOffsets.IsEmpty() || LocalSpawnOffsets.Num() > 8 || !SummonClass) { return false; }
	OutPlan.PawnClass = SummonClass;
	OutPlan.Lifetime = SummonLifetime;
	OutPlan.StartupTimeout = StartupTimeout;
	OutPlan.MaximumOwned = MaximumOwnedSummons;
	const FTransform Anchor(FRotator(0.f, Source->GetActorRotation().Yaw, 0.f), Source->GetActorLocation());
	UNavigationSystemV1* Navigation = bRequireNavigation ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	if (bRequireNavigation && !Navigation) { return false; }
	const float HalfHeight = SummonClass->GetDefaultObject<ARPGNonPlayerCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	for (const FVector Offset : LocalSpawnOffsets)
	{
		if (Offset.ContainsNaN() || Offset.SizeSquared() > FMath::Square(2000.f)) { return false; }
		FVector Location = Anchor.TransformPosition(Offset);
		if (Navigation)
		{
			FNavLocation Ground;
			if (!Navigation->ProjectPointToNavigation(Location - FVector(0.f, 0.f, HalfHeight), Ground, FVector(100.f, 100.f, 250.f))) { return false; }
			Location = Ground.Location + FVector(0.f, 0.f, HalfHeight + 2.f);
		}
		OutPlan.Transforms.Add(FTransform(Anchor.GetRotation(), Location));
	}
	return URPGBossSummonComponent::IsPlanValid(OutPlan);
}

bool URPGBossSummonAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const APawn* Source = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	const AAIController* AI = Source ? Cast<AAIController>(Source->GetController()) : nullptr;
	const URPGBossAIComponent* BossStateComponent = Source ? Source->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const URPGBossSummonComponent* SummonRegistry = Source ? Source->FindComponentByClass<URPGBossSummonComponent>() : nullptr;
	FRPGBossSummonPlan Plan;
	return AI && AI->GetGenericTeamId() != FGenericTeamId::NoTeam && BossStateComponent && !BossStateComponent->GetTelegraphState().bActive &&
		SummonRegistry && ActionTag.IsValid() && FMath::IsFinite(WindupSeconds) && WindupSeconds >= .01f && WindupSeconds <= 120.f &&
		SummonRegistry->HasCapacity(LocalSpawnOffsets.Num(), MaximumOwnedSummons) && MakePlan(Source, Plan) &&
		Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void URPGBossSummonAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (!IsActive() || !IsBossExecutionValid()) { return; }
	const uint64 Serial = GetBossExecutionSerial();
	APawn* Source = Cast<APawn>(GetAvatarActorFromActorInfo());
	Registry = Source ? Source->FindComponentByClass<URPGBossSummonComponent>() : nullptr;
	bReleased = false;
	const bool bCommitted = Registry.IsValid() && MakePlan(Source, LockedPlan) &&
		Registry->HasCapacity(LockedPlan.Transforms.Num(), LockedPlan.MaximumOwned) && CommitAbility(Handle, ActorInfo, ActivationInfo);
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	if (!bCommitted || !IsBossExecutionValid()) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	LockedEncounter = GetBossExecutionEncounterId();
	const int32 Sequence = StartBossTelegraph(ActionTag, Source->GetActorLocation(), WindupSeconds);
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	LockedSequence = Sequence;
	if (Sequence == 0) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	GetWorld()->GetTimerManager().SetTimer(CastTimer, FTimerDelegate::CreateWeakLambda(this, [this, Serial]() { ReleaseSummons(Serial); }), WindupSeconds, false);
}

void URPGBossSummonAbility::ReleaseSummons(uint64 Serial)
{
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	const APawn* Source = Cast<APawn>(GetAvatarActorFromActorInfo());
	const URPGBossAIComponent* BossStateComponent = Source ? Source->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const FRPGBossTelegraphState Telegraph = BossStateComponent ? BossStateComponent->GetTelegraphState() : FRPGBossTelegraphState();
	if (!IsBossExecutionValid() || !Registry.IsValid() || !Telegraph.bActive || Telegraph.Sequence != LockedSequence ||
		Telegraph.EncounterId != LockedEncounter || !FinishBossTelegraph())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return;
	}
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	if (!IsBossExecutionValid()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	// Spawn/construction callbacks may end this ability and clear its stored plan.
	const FRPGBossSummonPlan Plan = LockedPlan;
	const bool bSpawned = Registry->SpawnWave(this, Plan, LockedSequence);
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	bReleased = bSpawned;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bSpawned);
}

void URPGBossSummonAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) { return; }
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CastTimer); }
	if (!bReleased && Registry.IsValid()) { Registry->RemoveWave(LockedEncounter, LockedSequence); }
	Registry.Reset(); LockedPlan = FRPGBossSummonPlan(); LockedSequence = 0; LockedEncounter = 0; bReleased = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
