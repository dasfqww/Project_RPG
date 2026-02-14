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
	void BroadcastChangedMessage(EEquipmentSlotType SlotType, URPGItemBase* Item, int32 Count);
	
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
 * 장비 슬롯 데이터 관리 컴포넌트
 * 아이템의 슬롯 이동, 병합, 장착 규칙 검증을 담당
 */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGEquipmentComponent : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
	
public:
	URPGEquipmentComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	
public:
	/** 장착 가능 여부 검증 */
	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	int32 CanAddEquipment(URPGItemBase* Item, EEquipmentSlotType ToSlot) const;

	/** 아이템 추가/제거 (서버 전용) */
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
	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	URPGItemBase* GetItemInSlot(EEquipmentSlotType SlotType) const;
	
	UFUNCTION(BlueprintPure, Category = "RPG|Equipment")
	int32 GetItemCountInSlot(EEquipmentSlotType SlotType) const;

	const TArray<FRPGEquipmentEntry>& GetAllEntries() const { return EquipmentList.Entries; }

	URPGEquipComponent* GetEquipComponent() const;

public:
	FOnRPGEquipmentChanged OnEquipmentChanged;
	
private:
	UPROPERTY(Replicated)
	FRPGEquipmentList EquipmentList;

	friend struct FRPGEquipmentList;
};
