#include "Component/RPGInventoryComponent.h"

#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Interface/PawnUIInterface.h"
#include "GameFramework/Pawn.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/RPGItemBase.h"
#include "UI/Inventory/RPGInventoryBase.h"

namespace
{
	const TCHAR* RetiredWriteMessage =
		TEXT("Legacy inventory writes are retired; this action requires a native item transaction.");
}

URPGInventoryComponent::URPGInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // RPC compatibility only; no legacy item state is replicated.
}

void URPGInventoryComponent::TryAddItem(ARPGPickUpBase* InPickup)
{
	if (IsValid(InPickup) && GetOwner())
	{
		if (URPGItemCommandComponent* Commands =
			GetOwner()->FindComponentByClass<URPGItemCommandComponent>())
		{
			Commands->ServerPickupItem(InPickup);
		}
	}
}

void URPGInventoryComponent::Server_RequestPickup_Implementation(ARPGPickUpBase* ItemPickup)
{
	TryAddItem(ItemPickup);
}

void URPGInventoryComponent::Server_ConsumeItem_Implementation(URPGItemBase* Item)
{
	// A legacy object is just an ID carrier. Never trust its quantity, category or rolled stats,
	// and never manufacture a native record from it or from the legacy character JSON.
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Item))
	{
		RejectClientRequest(TEXT("No authoritative item is available for this legacy reference."));
		return;
	}

	URPGInventoryProjectionComponent* Projection =
		GetOwner()->FindComponentByClass<URPGInventoryProjectionComponent>();
	URPGItemCommandComponent* Commands =
		GetOwner()->FindComponentByClass<URPGItemCommandComponent>();
	const IRPGItemRecordSource* Records = Projection
		? Projection->GetAuthoritativeRecordSource() : nullptr;
	FRPGItemRecord Record;
	if (!Commands || !Records || !Records->Find(Item->GetInstanceId(), Record)
		|| Record.GetLocation().ContainerType != ERPGItemContainerType::Inventory)
	{
		RejectClientRequest(TEXT("This legacy item has not been migrated to the native inventory."));
		return;
	}

	Commands->ServerConsumeItem(FGuid::NewGuid(), Record.GetItemId(), Record.GetRevision());
}

void URPGInventoryComponent::Server_DropItem_Implementation(URPGItemBase*, int32)
{
	RejectClientRequest(RetiredWriteMessage);
}

void URPGInventoryComponent::Server_MoveItem_Implementation(URPGItemBase*, int32)
{
	RejectClientRequest(RetiredWriteMessage);
}

void URPGInventoryComponent::Server_SplitItem_Implementation(URPGItemBase*, int32, int32)
{
	RejectClientRequest(RetiredWriteMessage);
}

void URPGInventoryComponent::Server_TransferItemQuantity_Implementation(
	URPGItemBase*, URPGItemBase*, int32)
{
	RejectClientRequest(RetiredWriteMessage);
}

void URPGInventoryComponent::OrganizeInventory(EItemCategory Category)
{
	if (!GetOwner()) return;
	if (GetOwner()->HasAuthority())
	{
		Server_OrganizeInventory_Implementation(Category);
	}
	else
	{
		Server_OrganizeInventory(Category);
	}
}

void URPGInventoryComponent::Server_OrganizeInventory_Implementation(EItemCategory)
{
	RejectClientRequest(RetiredWriteMessage);
}

void URPGInventoryComponent::ToggleInventoryMenu()
{
	if (ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwner()))
	{
		Controller->ToggleInventory();
	}
}

TArray<URPGItemBase*> URPGInventoryComponent::GetAllItems() const
{
	return {}; // Do not materialize a second mutable inventory from native records.
}

int32 URPGInventoryComponent::GetSlotCapacity(const EItemCategory Category) const
{
	switch (Category)
	{
	case EItemCategory::Equip:
	case EItemCategory::Consume:
	case EItemCategory::Craft:
		return RPGItemInventoryPolicy::SlotCapacity; // Shared bag, not three separate capacities.
	default:
		return 0;
	}
}

void URPGInventoryComponent::AddRepSubObj(UObject*)
{
	// Retired: only InventoryProjectionComponent replicates live inventory entries.
}

void URPGInventoryComponent::RemoveRepSubObj(UObject*)
{
}

void URPGInventoryComponent::NotifyItemUpdated(URPGItemBase*)
{
	// Retired: legacy object changes must not masquerade as native inventory updates.
}

bool URPGInventoryComponent::SpawnDroppedItem(URPGItemBase*, int32)
{
	// Spawning from a manifest would mint a new item on pickup without an atomic debit.
	return false;
}

void URPGInventoryComponent::RejectClientRequest(const FString& Reason)
{
	UE_LOG(LogTemp, Warning, TEXT("Inventory request rejected for %s: %s"),
		*GetNameSafe(GetOwner()), *Reason);
	Client_InventoryOperationRejected(Reason);
}

void URPGInventoryComponent::Client_InventoryOperationRejected_Implementation(const FString& Reason)
{
	OnInventoryRebuilt.Broadcast();
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	IPawnUIInterface* PawnUI = Controller ? Cast<IPawnUIInterface>(Controller->GetPawn()) : nullptr;
	if (PawnUI)
	{
		if (UPlayerUIComponent* PlayerUI = PawnUI->GetPlayerUIComponent())
		{
			PlayerUI->OnNoticeTextChanged.Broadcast(FText::FromString(Reason));
		}
	}
}
