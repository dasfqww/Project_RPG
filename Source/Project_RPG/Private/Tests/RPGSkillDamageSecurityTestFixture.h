#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "Interface/PawnUIInterface.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "RPGSkillDamageSecurityTestFixture.generated.h"

class UCapsuleComponent;
class URPGAbilitySystemComponent;
class URPGAttributeSet;
class UPawnUIComponent;

/** Isolated native combat target, never attached to production player content. */
UCLASS(Transient, NotBlueprintable, NotPlaceable)
class ARPGSkillDamageSecurityTestPawn final : public APawn,
	public IAbilitySystemInterface, public IPawnUIInterface
{
	GENERATED_BODY()
public:
	ARPGSkillDamageSecurityTestPawn();
	virtual void BeginPlay() override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual UPawnUIComponent* GetPawnUIComponent() const override;
	virtual UPlayerUIComponent* GetPlayerUIComponent() const override { return nullptr; }
	virtual UNPCUIComponent* GetNPCUIComponent() const override { return nullptr; }
	URPGAbilitySystemComponent* ASC() const { return TestASC; }
	URPGAttributeSet* Attributes() const { return TestAttributes; }
private:
	UPROPERTY() TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY() TObjectPtr<URPGAbilitySystemComponent> TestASC;
	UPROPERTY() TObjectPtr<URPGAttributeSet> TestAttributes;
	UPROPERTY() TObjectPtr<UPawnUIComponent> TestUI;
};

UCLASS(Transient, NotBlueprintable)
class URPGSkillDamageSecurityTestEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGSkillDamageSecurityTestEffect();
};

/** Deliberately unsafe authored shape, rejected before its direct HP modifier can run. */
UCLASS(Transient, NotBlueprintable)
class URPGSkillDamageSecurityExtraModifierEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGSkillDamageSecurityExtraModifierEffect();
};

/** Arms activation-local configuration only; production query/GE/HP paths remain intact.
 * Does not test authored BP input, montage timing, client RPCs or replication. */
UCLASS(Transient, NotBlueprintable)
class URPGSkillDamageSecurityTestAbility final : public URPGGameplayAbility_SkillContainer
{
	GENERATED_BODY()
public:
	void Configure(const FRPGSkillRuntimeSpec& Spec, FGameplayTag DamageEvent);
	void Finish();
	int32 HitCount() const { return ServerAppliedHitCount; }
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* RelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
};
