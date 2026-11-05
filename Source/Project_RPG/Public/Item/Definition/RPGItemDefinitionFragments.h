#pragma once

#include "CoreMinimal.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Type/RPGEnumTypes.h"
#include "RPGItemDefinitionFragments.generated.h"

class UGameplayEffect;
class URPGAbilitySet;

/** Shared trigger data; execution belongs to the granted equipment ability. */
UCLASS(Abstract, BlueprintType, Const, DefaultToInstanced, EditInlineNew)
class PROJECT_RPG_API URPGItemHitTriggerDefinitionFragment : public URPGItemDefinitionFragment
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trigger")
	FGameplayTag RequiredSkillTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trigger")
	bool bRequireMaximumCharge = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trigger")
	bool bRequireFinisher = false;
};

UCLASS(BlueprintType, Const, DefaultToInstanced, EditInlineNew)
class PROJECT_RPG_API URPGItemShieldOnHitDefinitionFragment : public URPGItemHitTriggerDefinitionFragment
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float MaximumHealthFraction = 0.05f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "0.01"))
	float DurationSeconds = 3.f;
};

UCLASS(BlueprintType, Const, DefaultToInstanced, EditInlineNew)
class PROJECT_RPG_API URPGItemResolveOnHitDefinitionFragment : public URPGItemHitTriggerDefinitionFragment
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Resource", meta = (ClampMin = "0.01"))
	float Amount = 20.f;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemStatRange
{
	GENERATED_BODY()

	bool IsValid() const;
	float Roll(FRandomStream& RandomStream) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		meta = (Categories = "Shared.Stat"))
	FGameplayTag StatTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float Minimum = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float Maximum = 0.0f;
};

/** Rolls persistent per-instance stats once when the item is created. */
UCLASS(BlueprintType, Const, DefaultToInstanced, EditInlineNew)
class PROJECT_RPG_API URPGItemStatDefinitionFragment
	: public URPGItemDefinitionFragment
{
	GENERATED_BODY()

public:
	virtual void BuildInstanceState(
		FRPGItemInstanceStateBuilder& Builder,
		FRandomStream& RandomStream) const override;

#if WITH_EDITOR
	virtual EDataValidationResult ValidateDefinition(
		const URPGItemDefinition& Definition,
		FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly,
		meta = (TitleProperty = "StatTag"))
	TArray<FRPGItemStatRange> StatRanges;
};

/** Equipment-only authored data. Runtime equip services consume this fragment. */
UCLASS(BlueprintType, Const, DefaultToInstanced, EditInlineNew)
class PROJECT_RPG_API URPGItemEquipmentDefinitionFragment
	: public URPGItemDefinitionFragment
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "RPG|Item|Equipment")
	bool CanEquipInSlot(EEquipmentSlotType SlotType) const;

	bool HasValidIdentityForSlot(EEquipmentSlotType SlotType) const;

#if WITH_EDITOR
	virtual EDataValidationResult ValidateDefinition(
		const URPGItemDefinition& Definition,
		FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSet<EEquipmentSlotType> CompatibleSlots;

	/** Count preserves unauthored metadata; readers must not invent a weapon/utility identity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Identity")
	ERPGGladiatorWeaponType WeaponType = ERPGGladiatorWeaponType::Count;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Identity")
	EWeaponHandType WeaponHandType = EWeaponHandType::Count;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Identity")
	ERPGGladiatorUtilityType UtilityType = ERPGGladiatorUtilityType::Count;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSoftClassPtr<AActor> EquippedActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FName AttachSocket;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	FTransform EquippedActorRelativeTransform;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TArray<TObjectPtr<URPGAbilitySet>> GrantedAbilitySets;
};

/** Consumable-only authored data. An item-use service owns effect execution. */
UCLASS(BlueprintType, Const, DefaultToInstanced, EditInlineNew)
class PROJECT_RPG_API URPGItemConsumableDefinitionFragment
	: public URPGItemDefinitionFragment
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EDataValidationResult ValidateDefinition(
		const URPGItemDefinition& Definition,
		FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable")
	TSubclassOf<UGameplayEffect> GameplayEffect;

	/** Per-definition values supplied to SetByCaller magnitudes on GameplayEffect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable")
	TMap<FGameplayTag, float> SetByCallerMagnitudes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable",
		meta = (ClampMin = "1"))
	int32 QuantityPerUse = 1;
};
