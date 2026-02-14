// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Equipment/RPGEquipmentComponent.h"
#include "Component/Equipment/RPGEquipComponent.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGPlayer.h"
#include "Net/UnrealNetwork.h"
#include "Engine/ActorChannel.h"

// --- FRPGEquipmentList Implementation ---

void FRPGEquipmentList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		const FRPGEquipmentEntry& Entry = Entries[Index];
		BroadcastChangedMessage(Entry.EquipmentSlotType, Entry.ItemInstance, Entry.ItemCount);
	}
}

void FRPGEquipmentList::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	for (int32 Index : ChangedIndices)
	{
		const FRPGEquipmentEntry& Entry = Entries[Index];
		BroadcastChangedMessage(Entry.EquipmentSlotType, Entry.ItemInstance, Entry.ItemCount);
	}
}

void FRPGEquipmentList::PostReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	// 필요 시 제거 알림 로직 추가
}

void FRPGEquipmentList::BroadcastChangedMessage(EEquipmentSlotType SlotType, URPGItemBase* Item, int32 Count)
{
	if (OwnerComponent)
	{
		OwnerComponent->OnEquipmentChanged.Broadcast(SlotType, Item, Count);
		
		// EquipComponent(상태 관리자)에게 데이터 변경을 알림
		if (URPGEquipComponent* EquipComp = OwnerComponent->GetEquipComponent())
		{
			// 데이터가 바뀌었으므로 시각적 요소 갱신 (무기 스왑 상태 등 고려)
			EquipComp->UpdateEquippedItems();
		}
	}
}

// --- URPGEquipmentComponent Implementation ---

URPGEquipmentComponent::URPGEquipmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), EquipmentList(this)
{
	SetIsReplicatedByDefault(true);
}

void URPGEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URPGEquipmentComponent, EquipmentList);
}

bool URPGEquipmentComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (FRPGEquipmentEntry& Entry : EquipmentList.Entries)
	{
		if (IsValid(Entry.ItemInstance))
		{
			WroteSomething |= Channel->ReplicateSubobject(Entry.ItemInstance, *Bunch, *RepFlags);
		}
	}

	return WroteSomething;
}

int32 URPGEquipmentComponent::CanAddEquipment(URPGItemBase* Item, EEquipmentSlotType ToSlot) const
{
	if (!Item) return 0;

	// TODO: ItemManifest의 ItemTag와 ToSlot 타입 비교 검증
	// 예: 무기 아이템이 머리 슬롯에 들어오려 하면 거부
	
	return Item->GetTotalQuantity();
}

void URPGEquipmentComponent::AddEquipment_Internal(EEquipmentSlotType SlotType, URPGItemBase* Item, int32 Count)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;

	// 기존 아이템 제거
	RemoveEquipment_Internal(SlotType);

	if (Item)
	{
		FRPGEquipmentEntry& NewEntry = EquipmentList.Entries.Add_GetRef(FRPGEquipmentEntry(SlotType, Item, Count));
		EquipmentList.MarkItemDirty(NewEntry);
		
		OnEquipmentChanged.Broadcast(SlotType, Item, Count);
	}
}

URPGItemBase* URPGEquipmentComponent::RemoveEquipment_Internal(EEquipmentSlotType SlotType)
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return nullptr;

	for (int32 i = 0; i < EquipmentList.Entries.Num(); ++i)
	{
		if (EquipmentList.Entries[i].EquipmentSlotType == SlotType)
		{
			URPGItemBase* ItemToReturn = EquipmentList.Entries[i].ItemInstance;
			EquipmentList.Entries.RemoveAt(i);
			EquipmentList.MarkArrayDirty();

			OnEquipmentChanged.Broadcast(SlotType, nullptr, 0);
			return ItemToReturn;
		}
	}

	return nullptr;
}

bool URPGEquipmentComponent::IsWeaponSlot(EEquipmentSlotType SlotType)
{
	return (uint8)SlotType >= (uint8)EEquipmentSlotType::Weapon_Primary_L && 
	       (uint8)SlotType <= (uint8)EEquipmentSlotType::Weapon_Secondary_R;
}

bool URPGEquipmentComponent::IsArmorSlot(EEquipmentSlotType SlotType)
{
	return (uint8)SlotType >= (uint8)EEquipmentSlotType::Head && 
	       (uint8)SlotType <= (uint8)EEquipmentSlotType::Hands;
}

bool URPGEquipmentComponent::IsUtilitySlot(EEquipmentSlotType SlotType)
{
	return (uint8)SlotType >= (uint8)EEquipmentSlotType::Utility_1 && 
	       (uint8)SlotType <= (uint8)EEquipmentSlotType::Utility_2;
}

bool URPGEquipmentComponent::IsPrimaryWeaponSlot(EEquipmentSlotType SlotType)
{
	return SlotType == EEquipmentSlotType::Weapon_Primary_L || SlotType == EEquipmentSlotType::Weapon_Primary_R;
}

bool URPGEquipmentComponent::IsSecondaryWeaponSlot(EEquipmentSlotType SlotType)
{
	return SlotType == EEquipmentSlotType::Weapon_Secondary_L || SlotType == EEquipmentSlotType::Weapon_Secondary_R;
}

URPGItemBase* URPGEquipmentComponent::GetItemInSlot(EEquipmentSlotType SlotType) const
{
	for (const auto& Entry : EquipmentList.Entries)
	{
		if (Entry.EquipmentSlotType == SlotType)
		{
			return Entry.ItemInstance;
		}
	}
	return nullptr;
}

int32 URPGEquipmentComponent::GetItemCountInSlot(EEquipmentSlotType SlotType) const
{
	for (const auto& Entry : EquipmentList.Entries)
	{
		if (Entry.EquipmentSlotType == SlotType)
		{
			return Entry.ItemCount;
		}
	}
	return 0;
}

URPGEquipComponent* URPGEquipmentComponent::GetEquipComponent() const
{
	if (AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<URPGEquipComponent>();
	}
	return nullptr;
}
