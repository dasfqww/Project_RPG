#include "Ability/RPGBossSummonAbility.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "AIController.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

bool URPGBossSummonAbility::ValidatePatternSettings(const URPGBossPatternSettings* Settings, FText& OutError) const
{
	if (!Settings) return true;
	const URPGBossSummonPatternSettings* Summon = Cast<URPGBossSummonPatternSettings>(Settings);
	if (!Summon || Summon->LocalSpawnOffsets.IsEmpty() || Summon->LocalSpawnOffsets.Num() > 8 ||
		Summon->LocalSpawnOffsets.Num() > MaximumOwnedSummons ||
		!FMath::IsFinite(Summon->WindupSeconds) || Summon->WindupSeconds < .01f || Summon->WindupSeconds > 120.f ||
		!FMath::IsFinite(Summon->Lifetime) || Summon->Lifetime < .1f || Summon->Lifetime > 300.f ||
		Summon->LocalSpawnOffsets.ContainsByPredicate([](const FVector& Offset) { return Offset.ContainsNaN() || Offset.SizeSquared() > FMath::Square(2000.f); }))
	{
		OutError = FText::FromString(TEXT("Summon settings require 1..8 bounded offsets within the ownership cap, windup 0.01..120s and lifetime 0.1..300s."));
		return false;
	}
	return true;
}

bool URPGBossSummonAbility::MakePlan(const APawn* Source, FRPGBossSummonPlan& OutPlan, const URPGBossSummonPatternSettings* Settings) const
{
	OutPlan = FRPGBossSummonPlan();
	const TArray<FVector>& Offsets = Settings ? Settings->LocalSpawnOffsets : LocalSpawnOffsets;
	if (!IsValid(Source) || Source->GetActorLocation().ContainsNaN() || Source->GetActorRotation().ContainsNaN() ||
		Offsets.IsEmpty() || Offsets.Num() > 8 || !SummonClass) { return false; }
	OutPlan.PawnClass = SummonClass;
	OutPlan.Lifetime = Settings ? Settings->Lifetime : SummonLifetime;
	OutPlan.StartupTimeout = StartupTimeout;
	OutPlan.MaximumOwned = MaximumOwnedSummons;
	const FTransform Anchor(FRotator(0.f, Source->GetActorRotation().Yaw, 0.f), Source->GetActorLocation());
	UNavigationSystemV1* Navigation = bRequireNavigation ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	if (bRequireNavigation && !Navigation) { return false; }
	const float HalfHeight = SummonClass->GetDefaultObject<ARPGNonPlayerCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	for (const FVector Offset : Offsets)
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
	const URPGBossPatternSettings* PendingSettings = GetPendingPatternSettings(Handle, ActorInfo);
	FText Error;
	if (!ValidatePatternSettings(PendingSettings, Error)) return false;
	const URPGBossSummonPatternSettings* Settings = Cast<URPGBossSummonPatternSettings>(PendingSettings);
	const float CastSeconds = Settings ? Settings->WindupSeconds : WindupSeconds;
	FRPGBossSummonPlan Plan;
	return AI && AI->GetGenericTeamId() != FGenericTeamId::NoTeam && BossStateComponent && !BossStateComponent->GetTelegraphState().bActive &&
		SummonRegistry && ActionTag.IsValid() && FMath::IsFinite(CastSeconds) && CastSeconds >= .01f && CastSeconds <= 120.f &&
		MakePlan(Source, Plan, Settings) && SummonRegistry->HasCapacity(Plan.Transforms.Num(), MaximumOwnedSummons) &&
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
	const URPGBossSummonPatternSettings* Settings = Cast<URPGBossSummonPatternSettings>(GetBossPatternSettings());
	const float CastSeconds = Settings ? Settings->WindupSeconds : WindupSeconds;
	const bool bCommitted = Registry.IsValid() && MakePlan(Source, LockedPlan, Settings) &&
		Registry->HasCapacity(LockedPlan.Transforms.Num(), LockedPlan.MaximumOwned) && CommitAbility(Handle, ActorInfo, ActivationInfo);
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	if (!bCommitted || !IsBossExecutionValid()) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	LockedEncounter = GetBossExecutionEncounterId();
	const int32 Sequence = StartBossTelegraph(ActionTag, Source->GetActorLocation(), CastSeconds);
	if (!IsActive() || GetBossExecutionSerial() != Serial) { return; }
	LockedSequence = Sequence;
	if (Sequence == 0) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	GetWorld()->GetTimerManager().SetTimer(CastTimer, FTimerDelegate::CreateWeakLambda(this, [this, Serial]() { ReleaseSummons(Serial); }), CastSeconds, false);
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
