#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Type/RPGEnumTypes.h"
#include "RPGItemDefinition.generated.h"

class URPGItemDefinition;
class AActor;
class FDataValidationContext;
class UStaticMesh;
class UTexture2D;
struct FRPGItemInstanceState;

/**
 * One composable part of an item definition.
 *
 * Fragments author data and contribute only to creation-time instance state.
 * World spawning, UI binding, inventory mutation, and GAS execution belong to
 * dedicated services that read these fragments.
 */
UCLASS(Abstract, BlueprintType, Const, DefaultToInstanced, EditInlineNew)
class PROJECT_RPG_API URPGItemDefinitionFragment : public UObject
{
	GENERATED_BODY()

public:
	virtual void BuildInstanceState(
		class FRPGItemInstanceStateBuilder& Builder,
		FRandomStream& RandomStream) const;

	/** Override only when repeated fragments of the same concrete class are meaningful. */
	virtual bool AllowsMultipleFragments() const { return false; }

#if WITH_EDITOR
	virtual EDataValidationResult ValidateDefinition(
		const URPGItemDefinition& Definition,
		FDataValidationContext& Context) const;
#endif
};

/**
 * Immutable authored item data. Runtime containers hold URPGItemInstance
 * objects and reference this asset instead of copying the full definition.
 */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;
	static FPrimaryAssetId MakePrimaryAssetIdForTag(
		const FGameplayTag& InItemTag);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(
		FDataValidationContext& Context) const override;
#endif

	void BuildInstanceState(
		int32 RequestedQuantity,
		int32 GenerationSeed,
		FRPGItemInstanceState& OutState) const;

	UFUNCTION(BlueprintCallable, BlueprintPure = "false",
		meta = (DeterminesOutputType = "FragmentClass"))
	const URPGItemDefinitionFragment* FindFragmentByClass(
		TSubclassOf<URPGItemDefinitionFragment> FragmentClass) const;

	template <typename FragmentType>
	const FragmentType* FindFragmentByClass() const
	{
		static_assert(TIsDerivedFrom<FragmentType, URPGItemDefinitionFragment>::IsDerived);
		return Cast<FragmentType>(
			FindFragmentByClass(FragmentType::StaticClass()));
	}

	int32 GetMaxStackSize() const { return FMath::Max(1, MaxStackSize); }
	int32 GetDefinitionVersion() const { return FMath::Max(1, DefinitionVersion); }
	bool IsStackable() const { return GetMaxStackSize() > 1; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity",
		meta = (ClampMin = "1"))
	int32 DefinitionVersion = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity",
		meta = (Categories = "GameItems"))
	FGameplayTag ItemTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EItemCategory ItemCategory = EItemCategory::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World")
	TSoftObjectPtr<UStaticMesh> PickupMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "World")
	TSoftClassPtr<AActor> PickupActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory",
		meta = (ClampMin = "1"))
	int32 MaxStackSize = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	FIntPoint GridSize = FIntPoint(1, 1);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Traits")
	FGameplayTagContainer ItemTraits;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "Fragments")
	TArray<TObjectPtr<URPGItemDefinitionFragment>> Fragments;
};
