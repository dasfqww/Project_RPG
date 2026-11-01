// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Type/RPGEnumTypes.h"
#include "RPGEquipmentComponent.generated.h"

class URPGEquipmentComponent;
class URPGItemBase;
class ARPGPlayer;
class ARPGPlayerController;
class URPGEquipComponent;

/** 장비 데이터 변경 알림 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnRPGEquipmentChanged, EEquipmentSlotType, URPGItemBase*, int32/*Count*/);

/** 개별 장비 슬롯 엔트리 */
USTRUCT(BlueprintType)
struct FRPGEquipmentEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()
	
public:
	FRPGEquipmentEntry() {}
	FRPGEquipmentEntry(EEquipmentSlotType InSlot, URPGItemBase* InInstance, int32 InCount)
		: ItemInstance(InInstance), ItemCount(InCount), EquipmentSlotType(InSlot) {}

	URPGItemBase* GetItemInstance() const { return ItemInstance; }
	int32 GetItemCount() const { return ItemCount; }
	EEquipmentSlotType GetSlotType() const { return EquipmentSlotType; }
	
private:
	friend struct FRPGEquipmentList;
	friend class URPGEquipmentComponent;

	UPROPERTY()
	TObjectPtr<URPGItemBase> ItemInstance;

	UPROPERTY()
	int32 ItemCount = 0;

	UPROPERTY()
	EEquipmentSlotType EquipmentSlotType = EEquipmentSlotType::None;
};

/** 장비 리스트 (FastArray) */
USTRUCT(BlueprintType)
struct FRPGEquipmentList : public FFastArraySerializer
{
	GENERATED_BODY()

public:
	FRPGEquipmentList() : OwnerComponent(nullptr) { }
	FRPGEquipmentList(URPGEquipmentComponent* InOwner) : OwnerComponent(InOwner) { }
	
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FRPGEquipmentEntry, FRPGEquipmentList>(Entries, DeltaParams, *this);
	}

	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
	void PostReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	
private:
	friend class URPGEquipmentComponent;

	UPROPERTY()
	TArray<FRPGEquipmentEntry> Entries;
	
	UPROPERTY(NotReplicated)
	TObjectPtr<URPGEquipmentComponent> OwnerComponent;
};

template<>
struct TStructOpsTypeTraits<FRPGEquipmentList> : public TStructOpsTypeTraitsBase2<FRPGEquipmentList>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * Serialized Blueprint compatibility facade. It owns no live equipment state.
 * Raw legacy writes are retired; use native item commands and equipment snapshots.
 */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGEquipmentComponent : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
	
public:
	URPGEquipmentComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

public:
	/** Retired legacy validation; returns zero, not an unverified object quantity. */
	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	int32 CanAddEquipment(URPGItemBase* Item, EEquipmentSlotType ToSlot) const;

	/** Retired raw mutations; never infer a native transaction from legacy state. */
	void AddEquipment_Internal(EEquipmentSlotType SlotType, URPGItemBase* Item, int32 Count);
	URPGItemBase* RemoveEquipment_Internal(EEquipmentSlotType SlotType);

public:
	/** 슬롯 타입 체크 유틸리티 */
	static bool IsWeaponSlot(EEquipmentSlotType SlotType);
	static bool IsArmorSlot(EEquipmentSlotType SlotType);
	static bool IsUtilitySlot(EEquipmentSlotType SlotType);
	static bool IsPrimaryWeaponSlot(EEquipmentSlotType SlotType);
	static bool IsSecondaryWeaponSlot(EEquipmentSlotType SlotType);

	/** 데이터 조회 */
	UFUNCTION(BlueprintPure, Category = "RPG|Equipment", meta = (DeprecatedFunction,
		DeprecationMessage = "Use AuthoritativeEquipment.FindSnapshotInSlot; legacy item objects are retired."))
	URPGItemBase* GetItemInSlot(EEquipmentSlotType SlotType) const;
	
	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	int32 GetItemCountInSlot(EEquipmentSlotType SlotType) const;

	const TArray<FRPGEquipmentEntry>& GetAllEntries() const;

	URPGEquipComponent* GetEquipComponent() const;

public:
	FOnRPGEquipmentChanged OnEquipmentChanged;
	
};
