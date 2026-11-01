#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class AActor;
class ARPGPlayerWeapon;
class UAbilitySystemComponent;
class UGameplayAbility;
struct FAnimMontageInstance;
struct FGameplayAbilitySpec;
struct FGameplayEffectSpec;
struct FGameplayAbilityTargetDataHandle;

/** Test-facing view of an activation, not an API for driving gameplay. */
struct FRPGBossAttackChargeSnapshot
{
	float TimePerLevel = 0.f;
	int32 MinimumReleaseLevel = 0;
	int32 MaxChargeLevel = 0;
	float MaxHoldTime = 0.f;
	FName ReleaseSection;
	bool bInputPressed = false;
};

/** Keep implementation-specific reads outside the common damage/replication probe. */
class IRPGBossAttackHitEvidence
{
public:
	virtual ~IRPGBossAttackHitEvidence() = default;
	virtual bool ContainsServerHit(const FGameplayEffectSpec& Effect, const AActor* Target) const = 0;
	/** A lethal GE may already have removed collision; inspect its committed server hit, not a post-death re-query. */
	virtual bool ContainsCommittedServerHit(const FGameplayEffectSpec& Effect, const AActor* Target, const UGameplayAbility* ActiveAbility) const
	{ return ContainsServerHit(Effect, Target); }
};

class IRPGBossAttackChargeObservation
{
public:
	virtual ~IRPGBossAttackChargeObservation() = default;
	virtual bool ReadCharge(const UGameplayAbility* Ability, FRPGBossAttackChargeSnapshot& Out) const = 0;
};

struct FRPGBossAttackChannelSnapshot
{
	float ManualHoldSeconds = 0.f;
	float LoopSeconds = 0.f;
	float FrameAdvanceSeconds = 0.f;
	FName StopSection;
	bool bInputPressed = false;
};

class IRPGBossAttackChannelObservation
{
public:
	virtual ~IRPGBossAttackChannelObservation() = default;
	virtual bool ReadChannel(const UGameplayAbility* Ability, const FAnimMontageInstance* MontageInstance,
		FRPGBossAttackChannelSnapshot& Out) const = 0;
};

/** Authored identities and expected behavior. No mutable per-run state. */
struct FRPGBossAttackScenario
{
	const TCHAR* CommandFlag;
	const TCHAR* SkillName;
	const TCHAR* AbilityPath;
	const TCHAR* MontageName;
	FGameplayTag DamageEvent;
	bool bAimAtGround;
	double InputHoldSeconds;
	int32 ExpectedDamageCount; // Zero: variable-count combo/channel, not a single-release skill.
	FGameplayTag PrerequisiteTag;
	const IRPGBossAttackHitEvidence* HitEvidence;
	const IRPGBossAttackChargeObservation* ChargeObservation;
	TArray<FName> ObservedHoldSections;
	FName ObservedReleaseSection;
	const IRPGBossAttackChannelObservation* ChannelObservation = nullptr;

	bool IsSkill() const { return HitEvidence != nullptr; }
	bool IsCharged() const { return ChargeObservation != nullptr; }
	bool IsChanneling() const { return ChannelObservation != nullptr; }
};

namespace RPGBossAttackScenarios
{
	/** Queued notifies may catch up in one animation update; bound by real traversal. */
	bool AcceptsChannelCadence(double DeltaSeconds, float PreviousAdvanceSeconds,
		float FrameAdvanceSeconds, float LoopSeconds, int32 HitsInFrame);
	TConstArrayView<FRPGBossAttackScenario> All();
	int32 CountSelectedSkills(const TCHAR* CommandLine);
	const FRPGBossAttackScenario& Select(const TCHAR* CommandLine);
	const FRPGBossAttackScenario& Current();
	FGameplayAbilitySpec* FindAbility(UAbilitySystemComponent* ASC, const FRPGBossAttackScenario& Scenario);
	FGameplayTag ResolveInputTag(const ARPGPlayerWeapon* Weapon, const FRPGBossAttackScenario& Scenario);
	/** Authored skill identity for public quick-slot registration, not its legacy input tag. */
	FGameplayTag ResolveSkillTag(UAbilitySystemComponent* ASC, const FRPGBossAttackScenario& Scenario);
	/** Read-only current-aim gate; never activates a skill or supplies fabricated targets. */
	bool HasLiveAimCandidate(UAbilitySystemComponent* ASC, const FRPGBossAttackScenario& Scenario, const AActor* Victim, FVector ViewDirection);
	/** Read-only diagnostic of a received target before the authored policy validates it. */
	void ObserveSubmittedTarget(const UGameplayAbility* Ability, const FGameplayAbilityTargetDataHandle& Data, const AActor* ExpectedVictim);
}
