// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Equipment/RPGEquipComponent.h"
#include "Item/RPGItemBase.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Character/RPGPlayer.h"
#include "Controller/RPGPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Engine/ActorChannel.h"
#include "AbilitySystemComponent.h"
#include "RPGDebugHelper.h"

// --- FRPGEquipList Implementation ---

void FRPGEquipList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		if (EquipComponent)
		{
			EquipComponent->OnItemEquipped(Entries[Index]);
		}
	}
}

void FRPGEquipList::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	for (int32 Index : ChangedIndices)
	{
		// 아이템이 바뀌었을 경우의 처리 (보통 해제 후 재장착으로 처리되지만 안전을 위해)
	}
}

void FRPGEquipList::PostReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	// 삭제는 사전에 UnequipItem_Internal 등에서 로컬/서버 처리가 완료되었다고 가정
}

// --- URPGEquipComponent Implementation ---

URPGEquipComponent::URPGEquipComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), EquipList(this)
{
	SetIsReplicatedByDefault(true);
}

void URPGEquipComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(URPGEquipComponent, EquipList);
	DOREPLIFETIME(URPGEquipComponent, CurrentEquipState);
}

bool URPGEquipComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (FRPGEquipEntry& Entry : EquipList.Entries)
	{
		if (IsValid(Entry.ItemInstance))
		{
			WroteSomething |= Channel->ReplicateSubobject(Entry.ItemInstance, *Bunch, *RepFlags);
		}
	}

	return WroteSomething;
}

void URPGEquipComponent::EquipItem(EEquipmentSlotType SlotType, URPGItemBase* Item)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Item) return;

	// 이미 해당 슬롯에 아이템이 있다면 해제
	UnequipItem(SlotType);

	FRPGEquipEntry& NewEntry = EquipList.Entries.Add_GetRef(FRPGEquipEntry(SlotType, Item));
	EquipList.MarkItemDirty(NewEntry);

	// 서버에서 즉시 로직 실행
	OnItemEquipped(NewEntry);
}

void URPGEquipComponent::UnequipItem(EEquipmentSlotType SlotType)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;

	for (int32 i = 0; i < EquipList.Entries.Num(); ++i)
	{
		if (EquipList.Entries[i].EquipmentSlotType == SlotType)
		{
			OnItemUnequipped(EquipList.Entries[i]);
			EquipList.Entries.RemoveAt(i);
			EquipList.MarkArrayDirty();
			break;
		}
	}
}

void URPGEquipComponent::ChangeEquipState(EEquipState NewState)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || CurrentEquipState == NewState) return;

	EEquipState OldState = CurrentEquipState;
	CurrentEquipState = NewState;

	// 상태 변경에 따른 장비 갱신 (시각적 효과 등)
	UpdateEquippedItems();

	OnEquipStateChanged.Broadcast(OldState, NewState);
}

void URPGEquipComponent::UpdateEquippedItems()
{
	// 현재 상태에 맞게 모든 장착 액터의 가시성이나 기능을 업데이트
	// 예: Primary 상태면 Secondary 무기 액터를 숨김
	for (FRPGEquipEntry& Entry : EquipList.Entries)
	{
		if (Entry.SpawnedActor)
		{
			bool bShouldBeVisible = true;
			
			// 무기 슬롯인 경우 현재 상태와 비교
			if (Entry.EquipmentSlotType >= EEquipmentSlotType::Weapon_Primary_L && Entry.EquipmentSlotType <= EEquipmentSlotType::Weapon_Secondary_R)
			{
				const TArray<EEquipmentSlotType>& ActiveSlots = GetSlotsForState(CurrentEquipState);
				bShouldBeVisible = ActiveSlots.Contains(Entry.EquipmentSlotType);
			}

			Entry.SpawnedActor->SetActorHiddenInGame(!bShouldBeVisible);
		}
	}
}

ARPGPlayer* URPGEquipComponent::GetRPGPlayer() const
{
	return GetOwningPawn<ARPGPlayer>();
}

ARPGPlayerController* URPGEquipComponent::GetRPGPlayerController() const
{
	return GetOwningController<ARPGPlayerController>();
}

UAbilitySystemComponent* URPGEquipComponent::GetAbilitySystemComponent() const
{
	if (ARPGPlayer* Player = GetRPGPlayer())
	{
		return Player->GetAbilitySystemComponent();
	}
	return nullptr;
}

URPGItemBase* URPGEquipComponent::GetItemInSlot(EEquipmentSlotType SlotType) const
{
	for (const auto& Entry : EquipList.Entries)
	{
		if (Entry.EquipmentSlotType == SlotType)
		{
			return Entry.ItemInstance;
		}
	}
	return nullptr;
}

AActor* URPGEquipComponent::GetSpawnedActorInSlot(const EEquipmentSlotType SlotType) const
{
	for (const FRPGEquipEntry& Entry : EquipList.Entries)
	{
		if (Entry.EquipmentSlotType == SlotType)
		{
			return Entry.SpawnedActor;
		}
	}

	return nullptr;
}

bool URPGEquipComponent::FindEquippedWeapon(const EWeaponHandType HandType,
	URPGItemBase*& OutItem, AActor*& OutSpawnedActor, EEquipmentSlotType& OutSlotType) const
{
	OutItem = nullptr;
	OutSpawnedActor = nullptr;
	OutSlotType = EEquipmentSlotType::None;

	TArray<EEquipmentSlotType, TInlineAllocator<4>> CandidateSlots;
	const auto AddWeaponSet = [&CandidateSlots](const bool bPrimary)
	{
		CandidateSlots.Add(bPrimary
			? EEquipmentSlotType::Weapon_Primary_L
			: EEquipmentSlotType::Weapon_Secondary_L);
		CandidateSlots.Add(bPrimary
			? EEquipmentSlotType::Weapon_Primary_R
			: EEquipmentSlotType::Weapon_Secondary_R);
	};

	if (CurrentEquipState == EEquipState::WeaponSet_Secondary)
	{
		AddWeaponSet(false);
		AddWeaponSet(true);
	}
	else
	{
		AddWeaponSet(true);
		AddWeaponSet(false);
	}

	for (const EEquipmentSlotType CandidateSlot : CandidateSlots)
	{
		const bool bIsLeftSlot = CandidateSlot == EEquipmentSlotType::Weapon_Primary_L ||
			CandidateSlot == EEquipmentSlotType::Weapon_Secondary_L;
		if ((HandType == EWeaponHandType::LeftHand && !bIsLeftSlot) ||
			(HandType == EWeaponHandType::RightHand && bIsLeftSlot))
		{
			continue;
		}

		for (const FRPGEquipEntry& Entry : EquipList.Entries)
		{
			if (Entry.EquipmentSlotType != CandidateSlot || !Entry.ItemInstance)
			{
				continue;
			}

			OutItem = Entry.ItemInstance;
			OutSpawnedActor = Entry.SpawnedActor;
			OutSlotType = Entry.EquipmentSlotType;
			return true;
		}
	}

	return false;
}

const TArray<EEquipmentSlotType>& URPGEquipComponent::GetSlotsForState(EEquipState State)
{
	static TMap<EEquipState, TArray<EEquipmentSlotType>> StateToSlots;
	static const TArray<EEquipmentSlotType> EmptySlots;

	if (StateToSlots.IsEmpty())
	{
		StateToSlots.Add(EEquipState::WeaponSet_Primary, { EEquipmentSlotType::Weapon_Primary_L, EEquipmentSlotType::Weapon_Primary_R });
		StateToSlots.Add(EEquipState::WeaponSet_Secondary, { EEquipmentSlotType::Weapon_Secondary_L, EEquipmentSlotType::Weapon_Secondary_R });
		StateToSlots.Add(EEquipState::Utility, { EEquipmentSlotType::Utility_1, EEquipmentSlotType::Utility_2 });
	}

	const TArray<EEquipmentSlotType>* FoundSlots = StateToSlots.Find(State);
	return FoundSlots ? *FoundSlots : EmptySlots;
}

void URPGEquipComponent::OnRep_CurrentEquipState(EEquipState OldState)
{
	UpdateEquippedItems();
	OnEquipStateChanged.Broadcast(OldState, CurrentEquipState);
}

void URPGEquipComponent::OnItemEquipped(FRPGEquipEntry& Entry)
{
	if (!Entry.ItemInstance) return;

	// 1. Fragment를 통한 기본 효과 적용 (Stats 등)
	if (FEquipmentFragment* EquipFragment = Entry.ItemInstance->GetItemManifestMutable().GetFragmentOfTypeMutable<FEquipmentFragment>())
	{
		EquipFragment->OnEquip(GetRPGPlayerController());
	}

	// 2. 무기 액터 스폰 (예시 로직 - 실제로는 DataAsset 등을 참조해야 함)
	// TODO: ItemManifest에서 스폰할 액터 클래스와 소켓 정보를 가져와야 함
	
	// 3. GAS 연동 (Ability 부여 등)
	// TODO: 아이템에 포함된 Ability들을 ASC에 부여하고 핸들을 Entry.GrantedAbilityHandles에 저장

	UpdateEquippedItems();
	
	OnEquipItemChanged.Broadcast(Entry.EquipmentSlotType, Entry.ItemInstance, 1);
}

void URPGEquipComponent::OnItemUnequipped(FRPGEquipEntry& Entry)
{
	// 1. Fragment 효과 제거
	if (Entry.ItemInstance)
	{
		if (FEquipmentFragment* EquipFragment = Entry.ItemInstance->GetItemManifestMutable().GetFragmentOfTypeMutable<FEquipmentFragment>())
		{
			EquipFragment->OnUnequip(GetRPGPlayerController());
		}
	}

	// 2. 스폰된 액터 제거
	if (Entry.SpawnedActor)
	{
		Entry.SpawnedActor->Destroy();
		Entry.SpawnedActor = nullptr;
	}

	// 3. 부여된 GAS 효과/어빌리티 제거
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		for (const FGameplayAbilitySpecHandle& Handle : Entry.GrantedAbilityHandles)
		{
			ASC->ClearAbility(Handle);
		}
		Entry.GrantedAbilityHandles.Empty();

		if (Entry.GrantedEffectHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Entry.GrantedEffectHandle);
			Entry.GrantedEffectHandle = FActiveGameplayEffectHandle();
		}
	}

	OnEquipItemChanged.Broadcast(Entry.EquipmentSlotType, nullptr, 0);
}
