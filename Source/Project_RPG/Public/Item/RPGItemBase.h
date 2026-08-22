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

	UPROPERTY(ReplicatedUsing = OnRep_TotalQuantity)
	int32 TotalQuantity = 0;

	UPROPERTY(ReplicatedUsing = OnRep_SlotIndex)
	int32 SlotIndex = -1;

	UPROPERTY(Replicated)
	FGuid InstanceId;

protected:
	/*bool operator==(const FName& OtherID) const
	{
		return this->ID == OtherID;
	}*/

private:

	UPROPERTY(VisibleAnywhere, meta = (BaseStruct = "/Script/Project_RPG.RPGItemManifest"), Replicated)
	FInstancedStruct ItemManifest;

	UFUNCTION()
	void OnRep_TotalQuantity();

	UFUNCTION()
	void OnRep_SlotIndex();

	void NotifyOwningInventory();

public:

	FORCEINLINE const FItemManifest& GetItemManifest() const { return ItemManifest.Get<FItemManifest>(); }
	FORCEINLINE FItemManifest& GetItemManifestMutable() { return ItemManifest.GetMutable<FItemManifest>(); }
	FORCEINLINE int32 GetTotalQuantity() const { return TotalQuantity; }
	FORCEINLINE void SetTotalQuantity(int32 InQuantity) { TotalQuantity= InQuantity; }
	FORCEINLINE int32 GetSlotIndex() const { return SlotIndex; }
	FORCEINLINE void SetSlotIndex(int32 InSlotIndex) { SlotIndex = InSlotIndex; }
	FORCEINLINE const FGuid& GetInstanceId() const { return InstanceId; }
	FORCEINLINE void SetInstanceId(const FGuid& InInstanceId) { InstanceId = InInstanceId; }
};

template<typename FragmentType>
const FragmentType* GetFragment(const URPGItemBase* Item, const FGameplayTag& Tag)
{
	if (!IsValid(Item))return nullptr;
	
	const FItemManifest& Manifest = Item->GetItemManifest();

	return Manifest.GetFragmentOfTypeByTag<FragmentType>(Tag);
}
