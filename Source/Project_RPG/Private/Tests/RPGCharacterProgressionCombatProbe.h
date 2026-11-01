#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpecHandle.h"

class APlayerController;
class ARPGSkillDamageNetworkTarget;
class URPGAbilitySystemComponent;
class UQuickSlotComponent;
class URPGGameplayAbility_SkillContainer;

/** Observes authored hits/cooldowns and naturally earned Resolve on a restored Pawn.
 * Only fixture positioning, critical-chance initialization and input are supplied. */
class FRPGCharacterProgressionCombatProbe
{
public:
	~FRPGCharacterProgressionCombatProbe();
	bool Tick(APlayerController& PC, URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle,
		UQuickSlotComponent& Slots, const FString& Gates, const FString& Choice);
	const FString& Failure() const { return Error; }
private:
	bool Fail(const TCHAR* Reason);
	bool Prepare(APlayerController& PC, URPGAbilitySystemComponent& ASC);
	bool ObserveContributionHUD(APlayerController& PC);
	bool TickServer(APlayerController& PC, URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle,
		URPGGameplayAbility_SkillContainer& Skill, const FString& Gates, const FString& Choice);
	bool TickClient(APlayerController& PC, URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle,
		URPGGameplayAbility_SkillContainer& Skill, UQuickSlotComponent& Slots, const FString& Gates, const FString& Choice);
	TWeakObjectPtr<ARPGSkillDamageNetworkTarget> Targets[4];
	TWeakObjectPtr<URPGAbilitySystemComponent> Source;
	FDelegateHandle Effects[4], EventHandle;
	FString Error;
	int32 EffectCounts[4] = {}, EventCount = 0, Round = 0, Phase = 0;
	int32 ContributionHUDStep = 0;
	uint64 Serial = 0;
	float InitialMana = 0.f, ExpectedSetByCaller = 0.f, TotalDamage = 0.f, BaseDamage = 0.f, BoostDamage = 0.f;
	float ExpectedDuration = 0.f;
	double PhaseAt = 0.0;
	bool bPrepared = false, bSawReceivedCooldown = false, bPassed = false;
	bool bLoggedContributionHUDContext = false;
};
