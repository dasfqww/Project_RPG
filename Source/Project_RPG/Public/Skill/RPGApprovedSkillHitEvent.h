#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Item/Equipment/RPGEquipmentEffectContext.h"
#include "Combat/RPGApprovedHitContext.h"
#include "UObject/Object.h"
#include "RPGApprovedSkillHitEvent.generated.h"

class URPGGameplayAbility_SkillContainer;

/** Synchronous server-authored notification. Only the approved skill pipeline can populate it. */
UCLASS(Transient, NotBlueprintable)
class PROJECT_RPG_API URPGApprovedSkillHitEvent final : public UObject
{
	GENERATED_BODY()
	friend class URPGGameplayAbility_SkillContainer;
	friend class FRPGEquipmentResolveHitTest;
public:
	static FGameplayTag EventTag();
	bool IsCurrent() const;
	URPGGameplayAbility_SkillContainer* GetSkill() const;
	const FRPGEquipmentEffectContext& GetEquipment() const { return Equipment; }
	FGameplayTag GetSkillTag() const { return SkillTag; }
	uint64 GetActivationSerial() const { return ActivationSerial; }
	bool WasMaximumCharge() const { return bMaximumCharge; }
	bool WasFinisher() const { return bFinisher; }
private:
	TWeakObjectPtr<URPGGameplayAbility_SkillContainer> Skill;
	TWeakObjectPtr<AActor> Avatar;
	FRPGApprovedHitContext Lifetime;
	FRPGEquipmentEffectContext Equipment;
	FGameplayTag SkillTag;
	uint64 ActivationSerial = 0;
	bool bMaximumCharge = false;
	bool bFinisher = false;
};
