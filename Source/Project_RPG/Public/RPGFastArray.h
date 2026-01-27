// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "RPGFastArray.generated.h"

class URPGInventoryComponent;
class URPGItemBase;
class URPGItemComponent;
class ARPGPickUpBase;


/**
 * 
 */
//인벤토리에 들어갈 단일 엔트리
USTRUCT(BlueprintType)
struct FInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()
	
	FInventoryEntry(){}

private:
	friend struct FInventoryFastArray;
	friend URPGInventoryComponent;

	UPROPERTY()
	TObjectPtr<URPGItemBase> Item = nullptr;
};

//인벤토리의 아이템 리스트
USTRUCT(BlueprintType)
struct FInventoryFastArray : public FFastArraySerializer
{
	GENERATED_BODY()

	FInventoryFastArray() {}
	FInventoryFastArray(UActorComponent* InOwnerComponent):OwnerComponent(InOwnerComponent){}

	TArray<URPGItemBase*> GetAllItems() const;

#pragma region FFastArraySerializer

	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);

#pragma endregion

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FastArrayDeltaSerialize<FInventoryEntry, FInventoryFastArray>(Entries, DeltaParams, *this);
	}

	URPGItemBase* AddEntry(ARPGPickUpBase* ItemPickup);
	URPGItemBase* AddEntry(URPGItemBase* Item);
	void RemoveEntry(URPGItemBase* Item);
	URPGItemBase* FindFirstItemType(const FGameplayTag& ItemTag);

private:
	friend URPGInventoryComponent;

	UPROPERTY()
	TArray<FInventoryEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<UActorComponent> OwnerComponent = nullptr;
};

//인벤토리의 아이템 리스트
template<>
struct TStructOpsTypeTraits<FInventoryFastArray>: public TStructOpsTypeTraitsBase2<FInventoryFastArray>
{
	enum { WithNetDeltaSerializer = true };
};