#include "Component/RPGInventoryComponent.h"

#include "Character/RPGPlayer.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Interface/PawnUIInterface.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Item/Manifest/RPGItemManifest.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Item/RPGItemBase.h"
#include "Manager/DataManager.h"
#include "Manager/HttpWebManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"
#include "TimerManager.h"
#include "UI/Inventory/RPGInventoryBase.h"

#include "RPGDebugHelper.h"

namespace RPGInventory
{
	constexpr int32 InvalidCapacity = 0;
}

URPGInventoryComponent::URPGInventoryComponent()
	: InventoryList(this)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	bReplicateUsingRegisteredSubObjectList = true;
}

void URPGInventoryComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// A player's inventory is private state and must never be sent to unrelated clients.
	DOREPLIFETIME_CONDITION(ThisClass, InventoryList, COND_OwnerOnly);
}

void URPGInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningController = Cast<ARPGPlayerController>(GetOwner());
	if (!ensureAlwaysMsgf(
		OwningController.IsValid(),
		TEXT("RPGInventoryComponent must be owned by an RPGPlayerController.")))
	{
		return;
	}

	if (ARPGPlayer* Player = Cast<ARPGPlayer>(OwningController->GetPawn()))
	{
		CachedPawnUIInterface = Cast<IPawnUIInterface>(Player);
	}

	ConstructInventory();

	if (GetOwner()->HasAuthority())
	{
		RequestPersistentInventory();
	}
}

void URPGInventoryComponent::ReadyForReplication()
{
	Super::ReadyForReplication();

	if (!IsUsingRegisteredSubObjectList() || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// HTTP restore can finish before the component becomes replication-ready.
	for (URPGItemBase* Item : InventoryList.GetAllItems())
	{
		AddRepSubObj(Item);
	}
}

void URPGInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PersistenceSaveTimer);
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FlushInventorySave();
	}

	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UHttpWebManager* WebManager = GameInstance->GetSubsystem<UHttpWebManager>())
		{
			WebManager->OnCharacterInventoryLoaded.RemoveDynamic(
				this, &ThisClass::OnWebInventoryLoaded);
			WebManager->OnCharacterInventorySaved.RemoveDynamic(
				this, &ThisClass::OnWebInventorySaved);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void URPGInventoryComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void URPGInventoryComponent::ConstructInventory()
{
	if (!OwningController.IsValid() || !OwningController->IsLocalController())
	{
		return;
	}

	if (!IsValid(InventoryMenuClass))
	{
		UE_LOG(LogTemp, Error, TEXT("InventoryMenuClass is not configured on %s."), *GetNameSafe(this));
		return;
	}

	InventoryMenu = CreateWidget<URPGInventoryBase>(
		OwningController.Get(), InventoryMenuClass);
	if (InventoryMenu)
	{
		InventoryMenu->AddToViewport();
		DisplayInventory(bShowInventory);
	}
}

void URPGInventoryComponent::DisplayInventory(bool bShow)
{
	if (!InventoryMenu || !OwningController.IsValid())
	{
		return;
	}

	if (bShow)
	{
		InventoryMenu->SetVisibility(ESlateVisibility::Visible);
		OwningController->SetInputMode(FInputModeGameAndUI());
		OwningController->SetShowMouseCursor(true);
	}
	else
	{
		InventoryMenu->SetVisibility(ESlateVisibility::Hidden);
		OwningController->SetInputMode(FInputModeGameOnly());
		OwningController->SetShowMouseCursor(false);
	}
}

void URPGInventoryComponent::ToggleInventoryMenu()
{
	bShowInventory = !bShowInventory;
	DisplayInventory(bShowInventory);
}

TArray<URPGItemBase*> URPGInventoryComponent::GetAllItems() const
{
	return InventoryList.GetAllItems();
}

int32 URPGInventoryComponent::GetSlotCapacity(EItemCategory Category) const
{
	switch (Category)
	{
	case EItemCategory::Equip:
		return FMath::Max(EquipSlotCapacity, 1);
	case EItemCategory::Consume:
		return FMath::Max(ConsumeSlotCapacity, 1);
	case EItemCategory::Craft:
		return FMath::Max(CraftSlotCapacity, 1);
	default:
		return RPGInventory::InvalidCapacity;
	}
}

bool URPGInventoryComponent::IsInventoryItem(const URPGItemBase* Item) const
{
	return IsValid(Item) && InventoryList.Contains(Item);
}

int32 URPGInventoryComponent::GetMaxStackQuantity(const FItemManifest& Manifest) const
{
	if (const FStackableFragment* Stackable =
		Manifest.GetFragmentOfType<FStackableFragment>())
	{
		return FMath::Clamp(Stackable->GetMaxQuantity(), 1, MaxQuantityPerRequest);
	}

	return 1;
}

URPGItemBase* URPGInventoryComponent::FindItemAtSlot(
	EItemCategory Category, int32 SlotIndex, const URPGItemBase* IgnoredItem) const
{
	for (URPGItemBase* ExistingItem : InventoryList.GetAllItems())
	{
		if (!IsValid(ExistingItem) || ExistingItem == IgnoredItem)
		{
			continue;
		}

		if (ExistingItem->GetItemManifest().GetItemCategory() == Category
			&& ExistingItem->GetSlotIndex() == SlotIndex)
		{
			return ExistingItem;
		}
	}

	return nullptr;
}

int32 URPGInventoryComponent::FindFreeSlot(
	EItemCategory Category, int32 PreferredSlot) const
{
	const int32 Capacity = GetSlotCapacity(Category);
	if (Capacity <= 0)
	{
		return INDEX_NONE;
	}

	if (PreferredSlot >= 0 && PreferredSlot < Capacity
		&& !FindItemAtSlot(Category, PreferredSlot))
	{
		return PreferredSlot;
	}

	for (int32 SlotIndex = 0; SlotIndex < Capacity; ++SlotIndex)
	{
		if (!FindItemAtSlot(Category, SlotIndex))
		{
			return SlotIndex;
		}
	}

	return INDEX_NONE;
}

URPGItemBase* URPGInventoryComponent::CreateInventoryItem(
	const FItemManifest& Manifest, int32 Quantity, int32 SlotIndex,
	const FGuid& RequestedInstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Quantity <= 0
		|| SlotIndex < 0 || SlotIndex >= GetSlotCapacity(Manifest.GetItemCategory()))
	{
		return nullptr;
	}

	URPGItemBase* NewItem = Manifest.Manifest(GetOwner());
	if (!IsValid(NewItem))
	{
		return nullptr;
	}

	NewItem->SetTotalQuantity(Quantity);
	NewItem->SetSlotIndex(SlotIndex);
	NewItem->SetInstanceId(RequestedInstanceId.IsValid() ? RequestedInstanceId : FGuid::NewGuid());

	InventoryList.AddEntry(NewItem);
	OnItemAdded.Broadcast(NewItem);
	return NewItem;
}

void URPGInventoryComponent::RemoveInventoryItem(URPGItemBase* Item)
{
	if (!IsInventoryItem(Item))
	{
		return;
	}

	OnItemRemoved.Broadcast(Item);
	InventoryList.RemoveEntry(Item);
}

void URPGInventoryComponent::NotifyItemUpdated(URPGItemBase* Item)
{
	if (!IsValid(Item))
	{
		return;
	}

	OnItemUpdated.Broadcast(Item);
	OnInventoryUpdated.Broadcast();
}

void URPGInventoryComponent::TryAddItem(ARPGPickUpBase* InPickup)
{
	if (!IsValid(InPickup) || !GetOwner())
	{
		return;
	}

	if (GetOwner()->HasAuthority())
	{
		HandlePickupOnAuthority(InPickup);
	}
	else
	{
		Server_RequestPickup(InPickup);
	}
}

void URPGInventoryComponent::Server_RequestPickup_Implementation(
	ARPGPickUpBase* ItemPickup)
{
	HandlePickupOnAuthority(ItemPickup);
}

bool URPGInventoryComponent::IsPickupRequestValid(
	const ARPGPickUpBase* ItemPickup) const
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(ItemPickup)
		|| !ItemPickup->HasAuthority() || ItemPickup->GetWorld() != GetWorld()
		|| !ItemPickup->IsAvailableForPickup() || ItemPickup->IsHidden())
	{
		return false;
	}

	const APawn* Pawn = OwningController.IsValid() ? OwningController->GetPawn() : nullptr;
	return IsValid(Pawn)
		&& FVector::DistSquared(Pawn->GetActorLocation(), ItemPickup->GetActorLocation())
			<= FMath::Square(MaxPickupDistance);
}

void URPGInventoryComponent::HandlePickupOnAuthority(ARPGPickUpBase* ItemPickup)
{
	if (bEnableWebPersistence && !bPersistenceLoadFinished)
	{
		RejectClientRequest(TEXT("Inventory is still loading."));
		return;
	}

	if (!IsPickupRequestValid(ItemPickup) || !ItemPickup->TryClaimPickup())
	{
		RejectClientRequest(TEXT("The item can no longer be picked up."));
		return;
	}

	const FItemManifest& PickupManifest = ItemPickup->GetItemManifest();
	const EItemCategory Category = PickupManifest.GetItemCategory();
	if (!PickupManifest.GetItemTag().IsValid() || GetSlotCapacity(Category) <= 0)
	{
		ItemPickup->ReleasePickupClaim();
		RejectClientRequest(TEXT("The item has invalid inventory data."));
		return;
	}

	const FStackableFragment* PickupStack =
		PickupManifest.GetFragmentOfType<FStackableFragment>();
	const int32 RequestedQuantity = PickupStack
		? FMath::Clamp(PickupStack->GetQuantity(), 1, MaxQuantityPerRequest)
		: 1;
	const int32 MaxStack = GetMaxStackQuantity(PickupManifest);
	int32 Remaining = RequestedQuantity;

	if (PickupStack)
	{
		for (URPGItemBase* ExistingItem : InventoryList.GetAllItems())
		{
			if (Remaining <= 0)
			{
				break;
			}

			if (!IsValid(ExistingItem)
				|| !ExistingItem->GetItemManifest().GetItemTag().MatchesTagExact(
					PickupManifest.GetItemTag()))
			{
				continue;
			}

			const int32 Space = FMath::Max(
				0, GetMaxStackQuantity(ExistingItem->GetItemManifest())
					- ExistingItem->GetTotalQuantity());
			const int32 AddedQuantity = FMath::Min(Remaining, Space);
			if (AddedQuantity <= 0)
			{
				continue;
			}

			ExistingItem->SetTotalQuantity(
				ExistingItem->GetTotalQuantity() + AddedQuantity);
			Remaining -= AddedQuantity;
			NotifyItemUpdated(ExistingItem);
		}
	}

	while (Remaining > 0)
	{
		const int32 FreeSlot = FindFreeSlot(Category);
		if (FreeSlot == INDEX_NONE)
		{
			break;
		}

		const int32 StackQuantity = FMath::Min(Remaining, MaxStack);
		if (!CreateInventoryItem(PickupManifest, StackQuantity, FreeSlot))
		{
			break;
		}

		Remaining -= StackQuantity;
	}

	const int32 AcceptedQuantity = RequestedQuantity - Remaining;
	if (AcceptedQuantity <= 0)
	{
		ItemPickup->ReleasePickupClaim();
		RejectClientRequest(TEXT("Inventory is full."));
		return;
	}

	if (Remaining == 0)
	{
		ItemPickup->PickedUp();
	}
	else
	{
		ItemPickup->SetPickupQuantity(Remaining);
		ItemPickup->ReleasePickupClaim();
	}

	MarkInventoryDirty();
	if (GetOwner())
	{
		GetOwner()->ForceNetUpdate();
	}
}

void URPGInventoryComponent::Server_DropItem_Implementation(
	URPGItemBase* Item, int32 Quantity)
{
	if ((bEnableWebPersistence && !bPersistenceLoadFinished)
		|| !IsInventoryItem(Item) || Quantity <= 0
		|| Quantity > Item->GetTotalQuantity())
	{
		RejectClientRequest(TEXT("Invalid drop request."));
		return;
	}

	if (!SpawnDroppedItem(Item, Quantity))
	{
		RejectClientRequest(TEXT("The dropped item could not be created."));
		return;
	}

	const int32 NewQuantity = Item->GetTotalQuantity() - Quantity;
	if (NewQuantity == 0)
	{
		RemoveInventoryItem(Item);
	}
	else
	{
		Item->SetTotalQuantity(NewQuantity);
		NotifyItemUpdated(Item);
	}

	MarkInventoryDirty();
}

void URPGInventoryComponent::Server_ConsumeItem_Implementation(URPGItemBase* Item)
{
	if ((bEnableWebPersistence && !bPersistenceLoadFinished)
		|| !IsInventoryItem(Item) || Item->GetTotalQuantity() <= 0
		|| !Item->IsConsumable())
	{
		RejectClientRequest(TEXT("Invalid consume request."));
		return;
	}

	if (FConsumableFragment* Consumable =
		Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FConsumableFragment>())
	{
		Consumable->OnConsume(OwningController.Get());
	}
	else if (FConsumeModifier* LegacyConsumable =
		Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FConsumeModifier>())
	{
		LegacyConsumable->OnConsume(OwningController.Get());
	}

	const int32 NewQuantity = Item->GetTotalQuantity() - 1;
	if (NewQuantity == 0)
	{
		RemoveInventoryItem(Item);
	}
	else
	{
		Item->SetTotalQuantity(NewQuantity);
		NotifyItemUpdated(Item);
	}

	MarkInventoryDirty();
}

void URPGInventoryComponent::Server_MoveItem_Implementation(
	URPGItemBase* Item, int32 NewSlotIndex)
{
	if ((bEnableWebPersistence && !bPersistenceLoadFinished) || !IsInventoryItem(Item))
	{
		RejectClientRequest(TEXT("Invalid move request."));
		return;
	}

	const EItemCategory Category = Item->GetItemManifest().GetItemCategory();
	if (NewSlotIndex < 0 || NewSlotIndex >= GetSlotCapacity(Category))
	{
		RejectClientRequest(TEXT("The target slot is out of range."));
		return;
	}

	const int32 PreviousSlotIndex = Item->GetSlotIndex();
	if (PreviousSlotIndex == NewSlotIndex)
	{
		return;
	}

	if (URPGItemBase* DestinationItem =
		FindItemAtSlot(Category, NewSlotIndex, Item))
	{
		DestinationItem->SetSlotIndex(PreviousSlotIndex);
		NotifyItemUpdated(DestinationItem);
	}

	Item->SetSlotIndex(NewSlotIndex);
	NotifyItemUpdated(Item);
	MarkInventoryDirty();
}

void URPGInventoryComponent::Server_SplitItem_Implementation(
	URPGItemBase* Item, int32 SplitQuantity, int32 TargetSlotIndex)
{
	if ((bEnableWebPersistence && !bPersistenceLoadFinished)
		|| !IsInventoryItem(Item) || !Item->IsStackable()
		|| SplitQuantity <= 0 || SplitQuantity >= Item->GetTotalQuantity())
	{
		RejectClientRequest(TEXT("Invalid split request."));
		return;
	}

	const EItemCategory Category = Item->GetItemManifest().GetItemCategory();
	if (TargetSlotIndex < 0 || TargetSlotIndex >= GetSlotCapacity(Category)
		|| FindItemAtSlot(Category, TargetSlotIndex))
	{
		RejectClientRequest(TEXT("The split target slot is not available."));
		return;
	}

	URPGItemBase* SplitItem = CreateInventoryItem(
		Item->GetItemManifest(), SplitQuantity, TargetSlotIndex);
	if (!SplitItem)
	{
		RejectClientRequest(TEXT("The item could not be split."));
		return;
	}

	Item->SetTotalQuantity(Item->GetTotalQuantity() - SplitQuantity);
	NotifyItemUpdated(Item);
	MarkInventoryDirty();
}

void URPGInventoryComponent::Server_TransferItemQuantity_Implementation(
	URPGItemBase* SourceItem, URPGItemBase* DestinationItem, int32 Quantity)
{
	if ((bEnableWebPersistence && !bPersistenceLoadFinished)
		|| SourceItem == DestinationItem || !IsInventoryItem(SourceItem)
		|| !IsInventoryItem(DestinationItem) || Quantity <= 0
		|| Quantity > SourceItem->GetTotalQuantity()
		|| !SourceItem->IsStackable() || !DestinationItem->IsStackable()
		|| !SourceItem->GetItemManifest().GetItemTag().MatchesTagExact(
			DestinationItem->GetItemManifest().GetItemTag()))
	{
		RejectClientRequest(TEXT("Invalid stack transfer request."));
		return;
	}

	const int32 DestinationSpace = GetMaxStackQuantity(
		DestinationItem->GetItemManifest()) - DestinationItem->GetTotalQuantity();
	const int32 TransferredQuantity = FMath::Min(Quantity, DestinationSpace);
	if (TransferredQuantity <= 0)
	{
		RejectClientRequest(TEXT("The destination stack is full."));
		return;
	}

	DestinationItem->SetTotalQuantity(
		DestinationItem->GetTotalQuantity() + TransferredQuantity);
	NotifyItemUpdated(DestinationItem);

	const int32 SourceRemainder =
		SourceItem->GetTotalQuantity() - TransferredQuantity;
	if (SourceRemainder == 0)
	{
		RemoveInventoryItem(SourceItem);
	}
	else
	{
		SourceItem->SetTotalQuantity(SourceRemainder);
		NotifyItemUpdated(SourceItem);
	}

	MarkInventoryDirty();
}

void URPGInventoryComponent::OrganizeInventory(EItemCategory Category)
{
	if (!GetOwner())
	{
		return;
	}

	if (GetOwner()->HasAuthority())
	{
		OrganizeInventoryOnAuthority(Category);
	}
	else
	{
		Server_OrganizeInventory(Category);
	}
}

void URPGInventoryComponent::Server_OrganizeInventory_Implementation(
	EItemCategory Category)
{
	if ((bEnableWebPersistence && !bPersistenceLoadFinished))
	{
		RejectClientRequest(TEXT("Inventory is still loading."));
		return;
	}

	OrganizeInventoryOnAuthority(Category);
}

void URPGInventoryComponent::OrganizeInventoryOnAuthority(EItemCategory Category)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const auto IsCategorySelected = [Category](const EItemCategory ItemCategory)
	{
		return Category == EItemCategory::None || Category == ItemCategory;
	};

	bool bInventoryChanged = false;

	for (const EItemCategory OrganizedCategory :
		{ EItemCategory::Equip, EItemCategory::Consume, EItemCategory::Craft })
	{
		if (!IsCategorySelected(OrganizedCategory))
		{
			continue;
		}

		const int32 Capacity = GetSlotCapacity(OrganizedCategory);
		if (Capacity <= 0)
		{
			continue;
		}

		TArray<URPGItemBase*> CategoryItems;
		for (URPGItemBase* Item : InventoryList.GetAllItems())
		{
			if (IsValid(Item) && Item->GetTotalQuantity() > 0
				&& Item->GetItemManifest().GetItemCategory() == OrganizedCategory)
			{
				CategoryItems.Add(Item);
			}
		}

		CategoryItems.Sort([](const URPGItemBase& A, const URPGItemBase& B)
		{
			const FString ATag = A.GetItemManifest().GetItemTag().ToString();
			const FString BTag = B.GetItemManifest().GetItemTag().ToString();
			if (ATag != BTag)
			{
				return ATag < BTag;
			}

			if (A.GetTotalQuantity() != B.GetTotalQuantity())
			{
				return A.GetTotalQuantity() > B.GetTotalQuantity();
			}

			return A.GetInstanceId().ToString(EGuidFormats::DigitsWithHyphens)
				< B.GetInstanceId().ToString(EGuidFormats::DigitsWithHyphens);
		});

		TArray<URPGItemBase*> OrganizedItems;
		TSet<URPGItemBase*> UpdatedItems;

		for (int32 GroupStart = 0; GroupStart < CategoryItems.Num();)
		{
			URPGItemBase* GroupItem = CategoryItems[GroupStart];
			if (!IsValid(GroupItem))
			{
				++GroupStart;
				continue;
			}

			int32 GroupEnd = GroupStart + 1;
			while (GroupEnd < CategoryItems.Num()
				&& CategoryItems[GroupEnd]->GetItemManifest().GetItemTag()
					.MatchesTagExact(GroupItem->GetItemManifest().GetItemTag()))
			{
				++GroupEnd;
			}

			if (!GroupItem->IsStackable())
			{
				for (int32 ItemIndex = GroupStart; ItemIndex < GroupEnd; ++ItemIndex)
				{
					OrganizedItems.Add(CategoryItems[ItemIndex]);
				}
			}
			else
			{
				int64 TotalQuantity = 0;
				const int32 MaxStack = GetMaxStackQuantity(
					GroupItem->GetItemManifest());
				for (int32 ItemIndex = GroupStart; ItemIndex < GroupEnd; ++ItemIndex)
				{
					TotalQuantity += FMath::Max(CategoryItems[ItemIndex]->GetTotalQuantity(), 0);
				}

				const int64 RequiredStacks64 =
					(TotalQuantity + MaxStack - 1) / MaxStack;
				const int32 GroupCount = GroupEnd - GroupStart;
				if (RequiredStacks64 > GroupCount)
				{
					// Preserve malformed or legacy quantities instead of silently losing data.
					UE_LOG(LogTemp, Warning,
						TEXT("Cannot merge an oversized %s stack group without extra slots."),
						*GroupItem->GetItemManifest().GetItemTag().ToString());
					for (int32 ItemIndex = GroupStart; ItemIndex < GroupEnd; ++ItemIndex)
					{
						OrganizedItems.Add(CategoryItems[ItemIndex]);
					}
				}
				else
				{
					const int32 RequiredStacks = FMath::Max(
						static_cast<int32>(RequiredStacks64), 1);
					for (int32 ItemOffset = 0; ItemOffset < GroupCount; ++ItemOffset)
					{
						URPGItemBase* Item = CategoryItems[GroupStart + ItemOffset];
						if (ItemOffset >= RequiredStacks)
						{
							RemoveInventoryItem(Item);
							bInventoryChanged = true;
							continue;
						}

						const int32 NewQuantity = static_cast<int32>(FMath::Min<int64>(
							TotalQuantity, MaxStack));
						TotalQuantity -= NewQuantity;
						if (Item->GetTotalQuantity() != NewQuantity)
						{
							Item->SetTotalQuantity(NewQuantity);
							UpdatedItems.Add(Item);
							bInventoryChanged = true;
						}
						OrganizedItems.Add(Item);
					}
				}
			}

			GroupStart = GroupEnd;
		}

		if (OrganizedItems.Num() > Capacity)
		{
			UE_LOG(LogTemp, Error,
				TEXT("Cannot organize %s inventory: %d items exceed capacity %d."),
				*UEnum::GetValueAsString(OrganizedCategory), OrganizedItems.Num(), Capacity);
			continue;
		}

		for (int32 ItemIndex = 0; ItemIndex < OrganizedItems.Num(); ++ItemIndex)
		{
			URPGItemBase* Item = OrganizedItems[ItemIndex];
			if (!IsValid(Item))
			{
				continue;
			}

			if (Item->GetSlotIndex() != ItemIndex)
			{
				Item->SetSlotIndex(ItemIndex);
				UpdatedItems.Add(Item);
				bInventoryChanged = true;
			}
		}

		for (URPGItemBase* UpdatedItem : UpdatedItems)
		{
			NotifyItemUpdated(UpdatedItem);
		}
	}

	if (bInventoryChanged)
	{
		OnInventoryRebuilt.Broadcast();
		MarkInventoryDirty();
		GetOwner()->ForceNetUpdate();
	}
}

void URPGInventoryComponent::RejectClientRequest(const FString& Reason)
{
	UE_LOG(LogTemp, Warning, TEXT("Inventory request rejected for %s: %s"),
		*GetNameSafe(GetOwner()), *Reason);
	Client_InventoryOperationRejected(Reason);
}

void URPGInventoryComponent::Client_InventoryOperationRejected_Implementation(
	const FString& Reason)
{
	OnInventoryRebuilt.Broadcast();

	if (CachedPawnUIInterface.IsValid())
	{
		if (UPlayerUIComponent* PlayerUI =
			CachedPawnUIInterface->GetPlayerUIComponent())
		{
			PlayerUI->OnNoticeTextChanged.Broadcast(FText::FromString(Reason));
		}
	}
}

void URPGInventoryComponent::AddRepSubObj(UObject* SubObj)
{
	if (IsUsingRegisteredSubObjectList() && IsReadyForReplication()
		&& IsValid(SubObj) && GetOwner() && GetOwner()->HasAuthority())
	{
		AddReplicatedSubObject(SubObj, COND_OwnerOnly);
	}
}

void URPGInventoryComponent::RemoveRepSubObj(UObject* SubObj)
{
	if (IsUsingRegisteredSubObjectList() && IsValid(SubObj)
		&& GetOwner() && GetOwner()->HasAuthority())
	{
		RemoveReplicatedSubObject(SubObj);
	}
}

bool URPGInventoryComponent::SpawnDroppedItem(
	URPGItemBase* Item, int32 Quantity)
{
	if (!IsInventoryItem(Item) || Quantity <= 0 || !OwningController.IsValid())
	{
		return false;
	}

	const APawn* OwningPawn = OwningController->GetPawn();
	if (!IsValid(OwningPawn))
	{
		return false;
	}

	FVector RotatedForward = OwningPawn->GetActorForwardVector().RotateAngleAxis(
		FMath::FRandRange(DropSpawnAngleMin, DropSpawnAngleMax), FVector::UpVector);
	FVector SpawnLocation =
		OwningPawn->GetActorLocation()
		+ RotatedForward * FMath::FRandRange(
			DropSpawnDistanceMin, DropSpawnDistanceMax);
	SpawnLocation.Z -= RelativeSpawnElevation;

	// Never mutate the inventory item's manifest while preparing a world pickup.
	FItemManifest DropManifest = Item->GetItemManifest();
	if (FStackableFragment* Stackable =
		DropManifest.GetFragmentOfTypeMutable<FStackableFragment>())
	{
		Stackable->SetQuantity(Quantity);
	}

	return DropManifest.SpawnPickupActor(
		this, SpawnLocation, FRotator::ZeroRotator);
}

FString URPGInventoryComponent::GetPersistenceCharacterId() const
{
	const ARPGPlayerState* PlayerState =
		OwningController.IsValid()
			? OwningController->GetPlayerState<ARPGPlayerState>()
			: nullptr;
	if (PlayerState && PlayerState->HasAuthenticatedCharacter())
	{
		return PlayerState->GetBackendCharacterId();
	}

	return FString();
}

void URPGInventoryComponent::InitializePersistenceForAuthenticatedCharacter()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		RequestPersistentInventory();
	}
}

void URPGInventoryComponent::RequestPersistentInventory()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (bPersistenceRequestStarted)
	{
		return;
	}

	if (!bEnableWebPersistence)
	{
		bPersistenceLoadFinished = true;
		bPersistenceLoadSucceeded = false;
		return;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager)
	{
		bPersistenceLoadFinished = true;
		UE_LOG(LogTemp, Error, TEXT("Inventory persistence subsystem is unavailable."));
		return;
	}

	PersistenceCharacterId = GetPersistenceCharacterId();
	if (PersistenceCharacterId.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("Inventory persistence is waiting for an authenticated character."));
		return;
	}

	bPersistenceRequestStarted = true;
	WebManager->OnCharacterInventoryLoaded.RemoveDynamic(
		this, &ThisClass::OnWebInventoryLoaded);
	WebManager->OnCharacterInventoryLoaded.AddDynamic(
		this, &ThisClass::OnWebInventoryLoaded);
	WebManager->OnCharacterInventorySaved.RemoveDynamic(
		this, &ThisClass::OnWebInventorySaved);
	WebManager->OnCharacterInventorySaved.AddDynamic(
		this, &ThisClass::OnWebInventorySaved);
	WebManager->LoadInventoryFromWeb(PersistenceCharacterId);
}

void URPGInventoryComponent::OnWebInventoryLoaded(
	const FString& CharacterId, const TArray<FItemSaveData>& LoadedData, bool bSuccess)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()
		|| CharacterId != PersistenceCharacterId)
	{
		return;
	}

	if (UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UHttpWebManager* WebManager =
			GameInstance->GetSubsystem<UHttpWebManager>())
		{
			WebManager->OnCharacterInventoryLoaded.RemoveDynamic(
				this, &ThisClass::OnWebInventoryLoaded);
		}
	}

	bPersistenceLoadFinished = true;
	bPersistenceLoadSucceeded = bSuccess;

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Inventory load failed for character %s; current state was preserved."),
			*CharacterId);
		return;
	}

	RestoreInventoryOnAuthority(LoadedData);
}

void URPGInventoryComponent::OnWebInventorySaved(
	const FString& CharacterId, bool bSuccess)
{
	if (CharacterId != PersistenceCharacterId || bSuccess || !GetWorld())
	{
		return;
	}

	bInventoryDirty = true;
	GetWorld()->GetTimerManager().SetTimer(
		PersistenceSaveTimer,
		this,
		&ThisClass::FlushInventorySave,
		FMath::Max(PersistenceSaveDelay * 5.f, 5.f),
		false);
}

void URPGInventoryComponent::RestoreInventoryOnAuthority(
	const TArray<FItemSaveData>& SaveData)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	UDataManager* DataManager = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UDataManager>()
		: nullptr;
	if (!DataManager)
	{
		UE_LOG(LogTemp, Error, TEXT("Cannot restore inventory without DataManager."));
		return;
	}

	for (URPGItemBase* ExistingItem : InventoryList.GetAllItems())
	{
		OnItemRemoved.Broadcast(ExistingItem);
	}
	InventoryList.ClearEntries();
	OnInventoryRebuilt.Broadcast();

	const int32 MaximumEntries =
		GetSlotCapacity(EItemCategory::Equip)
		+ GetSlotCapacity(EItemCategory::Consume)
		+ GetSlotCapacity(EItemCategory::Craft);
	const int32 EntriesToRead = FMath::Min(SaveData.Num(), MaximumEntries);
	TSet<FGuid> RestoredInstanceIds;

	for (int32 SaveIndex = 0; SaveIndex < EntriesToRead; ++SaveIndex)
	{
		const FItemSaveData& Data = SaveData[SaveIndex];
		const FGameplayTag ItemTag =
			FGameplayTag::RequestGameplayTag(Data.ItemID, false);
		if (!ItemTag.IsValid())
		{
			continue;
		}

		FItemManifest Manifest;
		if (!DataManager->GetItemManifestByTag(ItemTag, Manifest)
			|| GetSlotCapacity(Manifest.GetItemCategory()) <= 0)
		{
			continue;
		}

		const bool bStackable =
			Manifest.GetFragmentOfType<FStackableFragment>() != nullptr;
		int32 Remaining = bStackable
			? FMath::Clamp(Data.Quantity, 1, MaxQuantityPerRequest)
			: 1;
		const int32 MaxStack = GetMaxStackQuantity(Manifest);
		int32 PreferredSlot = Data.SlotIndex;

		FGuid RequestedInstanceId;
		if (!FGuid::Parse(Data.InstanceId, RequestedInstanceId)
			|| RestoredInstanceIds.Contains(RequestedInstanceId))
		{
			RequestedInstanceId.Invalidate();
		}

		bool bFirstStack = true;
		while (Remaining > 0)
		{
			const int32 SlotIndex = FindFreeSlot(
				Manifest.GetItemCategory(), bFirstStack ? PreferredSlot : INDEX_NONE);
			if (SlotIndex == INDEX_NONE)
			{
				break;
			}

			const int32 StackQuantity = FMath::Min(Remaining, MaxStack);
			const FGuid InstanceId = bFirstStack ? RequestedInstanceId : FGuid();
			URPGItemBase* NewItem = CreateInventoryItem(
				Manifest, StackQuantity, SlotIndex, InstanceId);
			if (!NewItem)
			{
				break;
			}

			RestoredInstanceIds.Add(NewItem->GetInstanceId());
			Remaining -= StackQuantity;
			PreferredSlot = INDEX_NONE;
			bFirstStack = false;
		}
	}

	bInventoryDirty = false;
	if (GetOwner())
	{
		GetOwner()->ForceNetUpdate();
	}
}

TArray<FItemSaveData> URPGInventoryComponent::GetInventorySaveData() const
{
	TArray<FItemSaveData> SaveData;

	for (URPGItemBase* Item : InventoryList.GetAllItems())
	{
		if (!IsValid(Item) || Item->GetTotalQuantity() <= 0)
		{
			continue;
		}

		FItemSaveData& NewData = SaveData.AddDefaulted_GetRef();
		const FGameplayTag ItemTag = Item->GetItemManifest().GetItemTag();
		NewData.ItemID = ItemTag.GetTagName();
		NewData.Quantity = Item->GetTotalQuantity();
		NewData.SlotIndex = Item->GetSlotIndex();
		NewData.Category = Item->GetItemManifest().GetItemCategory() == EItemCategory::None
			? FString()
			: ItemTag.ToString();
		NewData.InstanceId = Item->GetInstanceId().ToString(EGuidFormats::DigitsWithHyphens);
	}

	return SaveData;
}

void URPGInventoryComponent::MarkInventoryDirty()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bInventoryDirty = true;
	if (!bEnableWebPersistence || !bPersistenceLoadSucceeded || !GetWorld())
	{
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		PersistenceSaveTimer,
		this,
		&ThisClass::FlushInventorySave,
		PersistenceSaveDelay,
		false);
}

void URPGInventoryComponent::FlushInventorySave()
{
	if (!bInventoryDirty || !bEnableWebPersistence || !bPersistenceLoadSucceeded
		|| PersistenceCharacterId.IsEmpty())
	{
		return;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager)
	{
		return;
	}

	WebManager->SaveInventoryToWeb(
		GetInventorySaveData(), PersistenceCharacterId);
	bInventoryDirty = false;
}
