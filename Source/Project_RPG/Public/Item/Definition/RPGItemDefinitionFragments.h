#pragma once

#include "CoreMinimal.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "RPGItemDefinitionFragments.generated.h"

class UGameplayEffect;
class URPGAbilitySet;

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

#if WITH_EDITOR
	virtual EDataValidationResult ValidateDefinition(
		const URPGItemDefinition& Definition,
		FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSet<EEquipmentSlotType> CompatibleSlots;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Consumable",
		meta = (ClampMin = "1"))
	int32 QuantityPerUse = 1;
};
