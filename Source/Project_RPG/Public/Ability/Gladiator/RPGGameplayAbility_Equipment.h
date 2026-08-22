#pragma once

#include "Ability/RPGGameplayAbility.h"
#include "DataAsset/Definition/RPGGladiatorData.h"
#include "Type/RPGEnumTypes.h"
#include "RPGGameplayAbility_Equipment.generated.h"

class AActor;
class URPGItemBase;

/** Serialized counterpart of D1's FD1EquipmentInfo. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGGladiatorEquipmentInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "RPG|Gladiator|Equipment")
	ERPGGladiatorEquipmentType EquipmentType = ERPGGladiatorEquipmentType::Count;

	UPROPERTY(EditAnywhere, Category = "RPG|Gladiator|Equipment",
		meta = (EditCondition = "EquipmentType == ERPGGladiatorEquipmentType::Armor", EditConditionHides))
	ERPGGladiatorArmorType RequiredArmorType = ERPGGladiatorArmorType::Count;

	UPROPERTY(EditAnywhere, Category = "RPG|Gladiator|Equipment",
		meta = (EditCondition = "EquipmentType == ERPGGladiatorEquipmentType::Weapon", EditConditionHides))
	EWeaponHandType WeaponHandType = EWeaponHandType::Count;

	UPROPERTY(EditAnywhere, Category = "RPG|Gladiator|Equipment",
		meta = (EditCondition = "EquipmentType == ERPGGladiatorEquipmentType::Weapon", EditConditionHides))
	ERPGGladiatorWeaponType RequiredWeaponType = ERPGGladiatorWeaponType::Count;

	UPROPERTY(EditAnywhere, Category = "RPG|Gladiator|Equipment",
		meta = (EditCondition = "EquipmentType == ERPGGladiatorEquipmentType::Utility", EditConditionHides))
	ERPGGladiatorUtilityType RequiredUtilityType = ERPGGladiatorUtilityType::Count;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> EquipmentActor;

	UPROPERTY(Transient)
	TWeakObjectPtr<URPGItemBase> ItemInstance;
};

/**
 * D1 equipment-aware ability base adapted to the project's split Equip/Equipment/Combat components.
 * Explicit item or actor metadata is always enforced. The compatibility fallback is only used while
 * legacy items and weapon actors still have Count metadata or no spawned equipment actor.
 */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGGameplayAbility_Equipment : public URPGGameplayAbility
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_Equipment(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "RPG|Gladiator|Equipment")
	AActor* GetFirstEquipmentActor() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Gladiator|Equipment")
	URPGItemBase* GetEquipmentItemInstance(const AActor* InEquipmentActor) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Gladiator|Equipment")
	int32 GetEquipmentStatValue(FGameplayTag InStatTag, const AActor* InEquipmentActor) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Gladiator|Equipment")
	float GetSnapshottedAttackRate() const { return SnapshottedAttackRate; }

	UFUNCTION(BlueprintPure, Category = "RPG|Gladiator|Equipment")
	bool UsedEquipmentCompatibilityFallback() const { return bUsedCompatibilityFallback; }

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Equipment")
	TArray<FRPGGladiatorEquipmentInfo> EquipmentInfos;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Equipment", meta = (ClampMin = "0.0"))
	float DefaultAttackRate = 1.0f;

	/** Remove this fallback after class default equipment actors and item metadata are fully migrated. */
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Equipment|Compatibility")
	bool bAllowIncompleteEquipmentCompatibility = true;

private:
	enum class ERequirementResult : uint8
	{
		Satisfied,
		Missing,
		WrongType
	};

	ERequirementResult ResolveEquipmentRequirement(const AActor* AvatarActor,
		const FRPGGladiatorEquipmentInfo& EquipmentInfo, URPGItemBase*& OutItem,
		AActor*& OutEquipmentActor, bool& bOutUsedCompatibilityFallback) const;

	float SnapshottedAttackRate = 1.0f;
	bool bUsedCompatibilityFallback = false;
};
