#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGGameplayAbility.h"
#include "Item/Equipment/RPGEquipmentEffectContext.h"
#include "RPGEquipmentHitAbility.generated.h"

class URPGApprovedSkillHitEvent;
class URPGGameplayAbility_SkillContainer;
class URPGItemDefinition;
class URPGItemHitTriggerDefinitionFragment;
class URPGAuthoritativeEquipmentComponent;

/** Equipment-granted passive. Observes server facts and owns only its own effect resources. */
UCLASS(Abstract)
class PROJECT_RPG_API URPGEquipmentHitAbility : public URPGGameplayAbility
{
	GENERATED_BODY()
public:
	URPGEquipmentHitAbility();
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* Relevant = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* Info, const FGameplayAbilitySpec& Spec) override;
protected:
	virtual const URPGItemHitTriggerDefinitionFragment* GetTrigger(const URPGItemDefinition& Definition) const PURE_VIRTUAL(URPGEquipmentHitAbility::GetTrigger, return nullptr;);
	virtual void ApplyHitEffect(const URPGApprovedSkillHitEvent& Event, const URPGItemDefinition& Definition) PURE_VIRTUAL(URPGEquipmentHitAbility::ApplyHitEffect, );
	virtual void ReleaseEffect() {}
	FGuid EffectSource;
private:
	void HandleApprovedHit(const FGameplayEventData* Payload);
	void HandleEquipmentChanged();
	void HandleActiveStateChanged(EEquipState Previous, EEquipState Next);
	void Unbind();
	TWeakObjectPtr<UAbilitySystemComponent> ObservedASC;
	TWeakObjectPtr<URPGAuthoritativeEquipmentComponent> ObservedEquipment;
	TWeakObjectPtr<AActor> ObservedAvatar;
	FDelegateHandle HitHandle, EquipmentHandle, ActiveStateHandle;
	TMap<TWeakObjectPtr<URPGGameplayAbility_SkillContainer>, uint64> LastAwardedActivation;
	FRPGEquipmentEffectContext LastSource;
};

/** Reusable health-fraction shield proc, configured by the item's shield fragment. */
UCLASS()
class PROJECT_RPG_API URPGEquipmentShieldOnHitAbility final : public URPGEquipmentHitAbility
{
	GENERATED_BODY()
protected:
	virtual const URPGItemHitTriggerDefinitionFragment* GetTrigger(const URPGItemDefinition& Definition) const override;
	virtual void ApplyHitEffect(const URPGApprovedSkillHitEvent& Event, const URPGItemDefinition& Definition) override;
	virtual void ReleaseEffect() override;
private:
	TWeakObjectPtr<class URPGShieldComponent> OwnedShield;
};

/** Reuses the class-owned gauge and its unlock/berserk/authority checks. */
UCLASS()
class PROJECT_RPG_API URPGEquipmentResolveOnHitAbility final : public URPGEquipmentHitAbility
{
	GENERATED_BODY()
protected:
	virtual const URPGItemHitTriggerDefinitionFragment* GetTrigger(const URPGItemDefinition& Definition) const override;
	virtual void ApplyHitEffect(const URPGApprovedSkillHitEvent& Event, const URPGItemDefinition& Definition) override;
};
