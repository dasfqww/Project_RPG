#pragma once

#include "Ability/RPGGameplayAbility.h"
#include "GameplayEffect.h"
#include "Skill/RPGClassSkillPolicy.h"
#include "RPGGreatSwordResolveAbility.generated.h"

class URPGGameplayAbility_SkillContainer;
class URPGComboSkillAbility;

/** Dedicated instant modifier: the existing IdentityGauge remains the only gauge owner. */
UCLASS(NotBlueprintable)
class URPGGreatSwordResolveGaugeEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGGreatSwordResolveGaugeEffect();
};

/** Source-owned berserk state. No duration or periodic gauge drain. */
UCLASS(NotBlueprintable)
class URPGGreatSwordResolveReadyEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGGreatSwordResolveReadyEffect();
};

/** First-slice greatsword identity. Native confirmations only; no gauge/empower RPC. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGGreatSwordResolveAbility final : public URPGGameplayAbility, public IRPGClassSkillPolicy
{
	GENERATED_BODY()
public:
	URPGGreatSwordResolveAbility();
	virtual bool CanCommitClassSkill(const URPGGameplayAbility_SkillContainer& Skill) const override;
	virtual bool CommitClassSkill(URPGGameplayAbility_SkillContainer& Skill,
		float& OutDamageMultiplier, bool& OutSuppressHitGain) const override;
	virtual void OnClassSkillHitConfirmed(URPGGameplayAbility_SkillContainer& Skill) const override;
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;

	/** Includes locked progression so legacy per-target gains cannot bypass acquisition. */
	static bool IsConfiguredFor(const AActor* Avatar);
	static void AwardConfirmedSkillHit(URPGGameplayAbility_SkillContainer& Skill, float Amount = 20.f);
	static void AwardCompletedBasicCombo(URPGComboSkillAbility& Combo);
	/** Called once AFTER the skill commits. The result is frozen for all hits of that cast. */
	static bool CanCommitSkill(const URPGGameplayAbility_SkillContainer& Skill);
	static bool ConsumeForCommittedSkill(URPGGameplayAbility_SkillContainer& Skill, float& OutMultiplier, bool& OutSuppressGain);
	static bool IsBerserk(const AActor* Avatar);
	/** Trusted lifecycle callers only: death, avatar reuse and combat authority revocation. */
	static void RevokeFor(AActor* Avatar);

private:
	UFUNCTION()
	void FinishEntryMontage();
	static bool CanUseIdentity(const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilitySpecHandle Handle);
	static bool ApplyGaugeDelta(UGameplayAbility& SourceAbility, float Delta);
	bool HasCurrentOpportunity() const;
	void RemoveOpportunity();
	FActiveGameplayEffectHandle ReadyEffect;
	TWeakObjectPtr<UAbilitySystemComponent> ArmedASC;
	TWeakObjectPtr<AActor> ArmedAvatar;
	uint32 ArmedRestoreGeneration = 0, ArmedCombatRevision = 0, ArmedLifetimeEpoch = 0;
	bool bChangingState = false;
};
