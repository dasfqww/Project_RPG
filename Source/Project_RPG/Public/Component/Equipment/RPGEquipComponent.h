// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayEffectTypes.h"
#include "Type/RPGEnumTypes.h"
#include "RPGEquipComponent.generated.h"

class URPGEquipComponent;
class URPGItemBase;
class ARPGPlayer;
class ARPGPlayerController;
class AActor;
class UAbilitySystemComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRPGEquipStateChanged, EEquipState /*Old*/, EEquipState /*New*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnRPGEquipItemChanged, EEquipmentSlotType, URPGItemBase*, int32/*Count*/);

/** FastArray용 개별 슬롯 엔트리 */
USTRUCT(BlueprintType)
struct FRPGEquipEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()
	
public:
	FRPGEquipEntry() {}
	FRPGEquipEntry(EEquipmentSlotType InSlot, URPGItemBase* InInstance)
		: ItemInstance(InInstance), EquipmentSlotType(InSlot) {}

	URPGItemBase* GetItemInstance() const { return ItemInstance; }
	EEquipmentSlotType GetSlotType() const { return EquipmentSlotType; }
	AActor* GetSpawnedActor() const { return SpawnedActor; }

private:
	friend struct FRPGEquipList;
	friend class URPGEquipComponent;

	UPROPERTY()
	TObjectPtr<URPGItemBase> ItemInstance;

	UPROPERTY()
	EEquipmentSlotType EquipmentSlotType = EEquipmentSlotType::None;

	/** 장착 시 월드에 스폰된 액터 (무기 메쉬 등) */
	UPROPERTY(NotReplicated)
	TObjectPtr<AActor> SpawnedActor;

	/** 아이템에 의해 부여된 GAS 핸들들 */
	UPROPERTY(NotReplicated)
	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;

	UPROPERTY(NotReplicated)
	FActiveGameplayEffectHandle GrantedEffectHandle;
};

/** FastArray 리스트 구조체 */
USTRUCT(BlueprintType)
struct FRPGEquipList : public FFastArraySerializer
{
	GENERATED_BODY()

public:
	FRPGEquipList() : EquipComponent(nullptr) { }
	FRPGEquipList(URPGEquipComponent* InOwner) : EquipComponent(InOwner) { }
	
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FRPGEquipEntry, FRPGEquipList>(Entries, DeltaParams, *this);
	}

	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
	void PostReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	
private:
	friend class URPGEquipComponent;

	UPROPERTY()
	TArray<FRPGEquipEntry> Entries;
	
	UPROPERTY(NotReplicated)
	TObjectPtr<URPGEquipComponent> EquipComponent;
};

template<>
struct TStructOpsTypeTraits<FRPGEquipList> : public TStructOpsTypeTraitsBase2<FRPGEquipList>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * D1/Lyra 스타일의 고도화된 장비 관리 컴포넌트
 */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGEquipComponent : public UPawnExtensionComponentBase
{
	GENERATED_BODY()
	
public:
	URPGEquipComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	
public:
	/** 서버에서 아이템 장착 실행 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Equip")
	void EquipItem(EEquipmentSlotType SlotType, URPGItemBase* Item);

	/** 서버에서 아이템 해제 실행 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Equip")
	void UnequipItem(EEquipmentSlotType SlotType);

	/** 장비 상태 변경 (무기 스왑 등) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "RPG|Equip")
	void ChangeEquipState(EEquipState NewState);

	/** 현재 상태에서 활성화되어야 할 장비들을 갱신 (시각적/기능적) */
	void UpdateEquippedItems();

public:
	/** 유틸리티 함수 */
	ARPGPlayer* GetRPGPlayer() const;
	ARPGPlayerController* GetRPGPlayerController() const;
	UAbilitySystemComponent* GetAbilitySystemComponent() const;
	
	UFUNCTION(BlueprintPure, Category = "RPG|Equip")
	URPGItemBase* GetItemInSlot(EEquipmentSlotType SlotType) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Equip")
	EEquipState GetCurrentEquipState() const { return CurrentEquipState; }

	static const TArray<EEquipmentSlotType>& GetSlotsForState(EEquipState State);

public:
	FOnRPGEquipStateChanged OnEquipStateChanged;
	FOnRPGEquipItemChanged OnEquipItemChanged;
	
private:
	UFUNCTION()
	void OnRep_CurrentEquipState(EEquipState OldState);

	/** 실제 장착 로직 (GAS 부여, 액터 스폰 등) */
	void OnItemEquipped(FRPGEquipEntry& Entry);
	/** 실제 해제 로직 (GAS 제거, 액터 파괴 등) */
	void OnItemUnequipped(FRPGEquipEntry& Entry);

private:
	UPROPERTY(Replicated)
	FRPGEquipList EquipList;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentEquipState)
	EEquipState CurrentEquipState = EEquipState::None;

	friend struct FRPGEquipList;
};
