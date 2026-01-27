// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/Manifest/RPGItemManifest.h"
#include "RPGItemBase.generated.h"

class ARPGPlayer;
class URPGInventoryComponent;
class UGameplayEffect;
struct FStackableFragment;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGItemBase : public UObject
{
	GENERATED_BODY()
public:
	URPGItemBase();

	UPROPERTY()
	TObjectPtr<URPGInventoryComponent> OwningInventory;

	void ApplyConsumableEffect(ARPGPlayer* Player);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual bool IsSupportedForNetworking() const override { return true; }

	void SetItemManifest(const FItemManifest& Manifest);
	bool IsStackable() const;
	bool IsConsumable() const;

	UPROPERTY(Replicated)
	int32 TotalQuantity = 0;

	UPROPERTY(Replicated)
	int32 SlotIndex = -1;

protected:
	/*bool operator==(const FName& OtherID) const
	{
		return this->ID == OtherID;
	}*/

private:

	UPROPERTY(VisibleAnywhere, meta = (BaseStruct = "/Script/Project_RPG.RPGItemManifest"), Replicated)
	FInstancedStruct ItemManifest;

public:

	FORCEINLINE const FItemManifest& GetItemManifest() const { return ItemManifest.Get<FItemManifest>(); }
	FORCEINLINE FItemManifest& GetItemManifestMutable() { return ItemManifest.GetMutable<FItemManifest>(); }
	FORCEINLINE int32 GetTotalQuantity() const { return TotalQuantity; }
	FORCEINLINE void SetTotalQuantity(int32 InQuantity) { TotalQuantity= InQuantity; }
};

template<typename FragmentType>
const FragmentType* GetFragment(const URPGItemBase* Item, const FGameplayTag& Tag)
{
	if (!IsValid(Item))return nullptr;
	
	const FItemManifest& Manifest = Item->GetItemManifest();

	return Manifest.GetFragmentOfTypeByTag<FragmentType>(Tag);
}
