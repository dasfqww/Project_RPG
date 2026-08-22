// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Type/RPGEnumTypes.h"
#include "RPGItemFragment.generated.h"

class APlayerController;
class URPGCompositeBase;

USTRUCT(BlueprintType)
struct FItemFragment
{
	GENERATED_BODY();
	
	FItemFragment() {}
	FItemFragment(const FItemFragment&) = default;
	FItemFragment& operator=(const FItemFragment&) = default;
	FItemFragment(FItemFragment&&) = default;
	FItemFragment& operator=(FItemFragment&&) = default;
	virtual ~FItemFragment() {}
	
	FGameplayTag GetFragmentTag() const { return FragmentTag; }
	void SetFragmentTag(FGameplayTag Tag) { FragmentTag = Tag; }
	virtual void Manifest() {}

private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (Categories = "GameItems"))
	FGameplayTag FragmentTag = FGameplayTag::EmptyTag;
};

USTRUCT(BlueprintType)
struct FInventoryItemFragment : public FItemFragment
{
	GENERATED_BODY()

	virtual void Assimilate(URPGCompositeBase* Composite) const;
protected:
	bool MatchesWidgetTag(const URPGCompositeBase* Composite) const;
};


//USTRUCT(BlueprintType)
//struct FGridFragment : public FItemFragment
//{
//	GENERATED_BODY()
//
//	FIntPoint GetGridSize() const { return GridSize; }
//	void SetGridSize(const FIntPoint& Size) { GridSize = Size; }
//	float GetGridPadding() const { return GridPadding; }
//	void SetGridPadding(float Padding) { GridPadding = Padding; }
//
//private:
//
//	UPROPERTY(EditAnywhere, Category = "Inventory")
//	FIntPoint GridSize{ 1, 1 };
//
//	UPROPERTY(EditAnywhere, Category = "Inventory")
//	float GridPadding{ 0.f };
//
//};

USTRUCT(BlueprintType)
struct FImageFragment : public FInventoryItemFragment
{
	GENERATED_BODY()

	UTexture2D* GetIcon() const { return Icon; }
	virtual void Assimilate(URPGCompositeBase* Composite) const override;

private:

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TObjectPtr<UTexture2D> Icon{ nullptr };

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FVector2D IconDimensions{ 44.f, 44.f };
};

USTRUCT(BlueprintType)
struct FTextFragment : public FInventoryItemFragment
{
	GENERATED_BODY()

	FText GetText() const { return FragmentText; }
	void SetText(const FText& Text) { FragmentText = Text; }
	virtual void Assimilate(URPGCompositeBase* Composite) const override;

private:

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FText FragmentText;
};

USTRUCT(BlueprintType)
struct FLabeledNumberFragment : public FInventoryItemFragment
{
	GENERATED_BODY()

	virtual void Assimilate(URPGCompositeBase* Composite) const override;
	virtual void Manifest() override;

	// When manifesting for the first time, this fragment will randomize. However, onee equipped
	// and dropped, an item should retain the same value, so randomization should not occur.
	bool bRandomizeOnManifest{ true };

private:

	UPROPERTY(EditAnywhere, Category = "Inventory")
	FText Text_Label;

	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	float Value=0.f;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float Min = 0;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float Max = 0;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	bool bCollapseLabel = false;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	bool bCollapseValue = false;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MinFractionalDigits = 1;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MaxFractionalDigits = 1;
public:
	FORCEINLINE float GetValue() const { return Value; }
};

USTRUCT(BlueprintType)
struct FStackableFragment : public FItemFragment
{
	GENERATED_BODY()

	int32 GetMaxQuantity() const { return MaxQuantity; }
	int32 GetQuantity() const { return Quantity; }
	void SetQuantity(int32 InQuantity) { Quantity = InQuantity; }

private:

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 MaxQuantity = 1;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 Quantity = 1;
};

USTRUCT(BlueprintType)
struct FConsumeModifier : public FLabeledNumberFragment
{
	GENERATED_BODY()

	virtual void OnConsume(APlayerController* PC) {}
};

USTRUCT(BlueprintType)
struct FConsumableFragment : public FInventoryItemFragment
{
	GENERATED_BODY()
public:
	virtual void OnConsume(APlayerController* PC);
	virtual void Assimilate(URPGCompositeBase* Composite) const override;
	virtual void Manifest() override;
private:
	UPROPERTY(EditAnywhere, Category = "Inventory", meta = (ExcludeBaseStruct))
	TArray<TInstancedStruct<FConsumeModifier>> ConsumeModifiers;
};

USTRUCT(BlueprintType)
struct FHealthPotionFragment : public FConsumeModifier
{
	GENERATED_BODY()

	virtual void OnConsume(APlayerController* PC) override;
};

// Equipment
//

USTRUCT(BlueprintType)
struct FEquipModifier : public FLabeledNumberFragment
{
	GENERATED_BODY()

	virtual void OnEquip(APlayerController* PC) {}
	virtual void OnUnequip(APlayerController* PC) {}
};

USTRUCT(BlueprintType)
struct FStatModifier : public FEquipModifier
{
	GENERATED_BODY()

	virtual void OnEquip(APlayerController* PC) override;
	virtual void OnUnequip(APlayerController* PC) override;
};


USTRUCT(BlueprintType)
struct FEquipmentFragment : public FInventoryItemFragment
{
	GENERATED_BODY()

	bool bEquipped{ false };
	void OnEquip(APlayerController* PC);
	void OnUnequip(APlayerController* PC);
	virtual void Assimilate(URPGCompositeBase* Composite) const override;

	ERPGGladiatorEquipmentType GetEquipmentType() const { return EquipmentType; }
	EWeaponHandType GetWeaponHandType() const { return WeaponHandType; }
	ERPGGladiatorWeaponType GetWeaponType() const { return WeaponType; }
	ERPGGladiatorUtilityType GetUtilityType() const { return UtilityType; }

private:
	/**
	 * Optional D1 compatibility metadata. Count values deliberately mean "not migrated yet";
	 * equipment abilities may then use their explicitly marked compatibility path.
	 */
	UPROPERTY(EditAnywhere, Category = "Inventory|Equipment")
	ERPGGladiatorEquipmentType EquipmentType = ERPGGladiatorEquipmentType::Count;

	UPROPERTY(EditAnywhere, Category = "Inventory|Equipment",
		meta = (EditCondition = "EquipmentType == ERPGGladiatorEquipmentType::Weapon", EditConditionHides))
	EWeaponHandType WeaponHandType = EWeaponHandType::Count;

	UPROPERTY(EditAnywhere, Category = "Inventory|Equipment",
		meta = (EditCondition = "EquipmentType == ERPGGladiatorEquipmentType::Weapon", EditConditionHides))
	ERPGGladiatorWeaponType WeaponType = ERPGGladiatorWeaponType::Count;

	UPROPERTY(EditAnywhere, Category = "Inventory|Equipment",
		meta = (EditCondition = "EquipmentType == ERPGGladiatorEquipmentType::Utility", EditConditionHides))
	ERPGGladiatorUtilityType UtilityType = ERPGGladiatorUtilityType::Count;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TArray<TInstancedStruct<FEquipModifier>> EquipModifiers;
};
