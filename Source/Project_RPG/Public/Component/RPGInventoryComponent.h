#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Type/RPGEnumTypes.h"
#include "RPGInventoryComponent.generated.h"

class URPGItemBase;
class URPGInventoryBase;
class ARPGPickUpBase;

DECLARE_MULTICAST_DELEGATE(FOnInventoryUpdated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryItemChanged, URPGItemBase*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryRebuilt);

/**
 * Retired inventory compatibility facade for existing Blueprint/component references.
 * It owns no item list, replicated item subobjects, widget or persistence state.
 * Native records and ItemCommandComponent are the only live inventory path.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent), Blueprintable)
class PROJECT_RPG_API URPGInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGInventoryComponent();

	FOnInventoryUpdated OnInventoryUpdated;

	UFUNCTION(BlueprintCallable, Category = "Inventory",
		meta = (DeprecatedFunction, DeprecationMessage = "Use ItemCommandComponent.ServerPickupItem."))
	void TryAddItem(ARPGPickUpBase* InPickup);

	UFUNCTION(Server, Reliable)
	void Server_RequestPickup(ARPGPickUpBase* ItemPickup);

	/** Unsupported legacy writes reject without spawning or changing any item. */
	UFUNCTION(Server, Reliable)
	void Server_DropItem(URPGItemBase* Item, int32 Quantity);

	/** Forwards only an ID already present in the authoritative record source. */
	UFUNCTION(Server, Reliable)
	void Server_ConsumeItem(URPGItemBase* Item);

	UFUNCTION(Server, Reliable)
	void Server_MoveItem(URPGItemBase* Item, int32 NewSlotIndex);

	UFUNCTION(Server, Reliable)
	void Server_SplitItem(URPGItemBase* Item, int32 SplitQuantity, int32 TargetSlotIndex);

	UFUNCTION(Server, Reliable)
	void Server_TransferItemQuantity(
		URPGItemBase* SourceItem, URPGItemBase* DestinationItem, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category = "Inventory",
		meta = (DeprecatedFunction, DeprecationMessage = "Legacy inventory writes are retired."))
	void OrganizeInventory(EItemCategory Category = EItemCategory::None);

	UFUNCTION(Server, Reliable)
	void Server_OrganizeInventory(EItemCategory Category);

	/** Opens/closes the native projection window, never the legacy widget. */
	void ToggleInventoryMenu();

	/** Compatibility callbacks retained for legacy item/FastArray types. No state is kept. */
	void AddRepSubObj(UObject* SubObj);
	void RemoveRepSubObj(UObject* SubObj);
	bool SpawnDroppedItem(URPGItemBase* Item, int32 Quantity);
	void NotifyItemUpdated(URPGItemBase* Item);

	URPGInventoryBase* GetInventoryMenu() const { return nullptr; }

	UFUNCTION(BlueprintCallable, Category = "Inventory",
		meta = (DeprecatedFunction, DeprecationMessage = "Use InventoryProjectionComponent.GetProjectedItems."))
	TArray<URPGItemBase*> GetAllItems() const;

	UFUNCTION(BlueprintPure, Category = "Inventory",
		meta = (DeprecatedFunction, DeprecationMessage = "Native inventory has one shared bag, not per-category lists."))
	int32 GetSlotCapacity(EItemCategory Category) const;

	FOnInventoryItemChanged OnItemAdded;
	FOnInventoryItemChanged OnItemRemoved;
	FOnInventoryItemChanged OnItemUpdated;
	FOnInventoryRebuilt OnInventoryRebuilt;

private:
	void RejectClientRequest(const FString& Reason);

	UFUNCTION(Client, Reliable)
	void Client_InventoryOperationRejected(const FString& Reason);

	/** Serialized legacy setting retained for asset compatibility, never instantiated. */
	UPROPERTY()
	TSubclassOf<URPGInventoryBase> InventoryMenuClass;
};
