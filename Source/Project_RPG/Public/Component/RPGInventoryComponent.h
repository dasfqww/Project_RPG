// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGFastArray.h"
#include "Type/RPGStructTypes.h" // FItemSaveData 사용을 위해 필요
#include "RPGInventoryComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnInventoryUpdated);

class URPGItemBase;
class URPGInventoryBase;
class URPGItemComponent;
class ARPGPlayerController;
class ARPGPickUpBase;
class IPawnUIInterface;
struct FItemManifest;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryItemChanged, URPGItemBase*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryRebuilt);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable)
class PROJECT_RPG_API URPGInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	URPGInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FOnInventoryUpdated OnInventoryUpdated;

	/** Requests an authoritative pickup. Client-side capacity and quantity values are never trusted. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void TryAddItem(ARPGPickUpBase* InPickup);

	UFUNCTION(Server, Reliable)
	void Server_RequestPickup(ARPGPickUpBase* ItemPickup);

	UFUNCTION(Server, Reliable)
	void Server_DropItem(URPGItemBase* Item, int32 Quantity);

	UFUNCTION(Server, Reliable)
	void Server_ConsumeItem(URPGItemBase* Item);

	UFUNCTION(Server, Reliable)
	void Server_MoveItem(URPGItemBase* Item, int32 NewSlotIndex);

	UFUNCTION(Server, Reliable)
	void Server_SplitItem(URPGItemBase* Item, int32 SplitQuantity, int32 TargetSlotIndex);

	UFUNCTION(Server, Reliable)
	void Server_TransferItemQuantity(
		URPGItemBase* SourceItem, URPGItemBase* DestinationItem, int32 Quantity);

	/**
	 * Merges compatible stacks and compacts the selected category from slot zero.
	 * EItemCategory::None organizes every inventory category.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void OrganizeInventory(EItemCategory Category = EItemCategory::None);

	UFUNCTION(Server, Reliable)
	void Server_OrganizeInventory(EItemCategory Category);

	void ToggleInventoryMenu();
	void AddRepSubObj(UObject* SubObj);
	void RemoveRepSubObj(UObject* SubObj);
	bool SpawnDroppedItem(URPGItemBase* Item, int32 Quantity);
	void NotifyItemUpdated(URPGItemBase* Item);
	
	FORCEINLINE URPGInventoryBase* GetInventoryMenu() const { return InventoryMenu; }

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	TArray<URPGItemBase*> GetAllItems() const;

	/** Called by the server after a backend character identity is admitted. */
	void InitializePersistenceForAuthenticatedCharacter();

	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetSlotCapacity(EItemCategory Category) const;

	FOnInventoryItemChanged OnItemAdded; 
	FOnInventoryItemChanged OnItemRemoved;
	FOnInventoryItemChanged OnItemUpdated;
	FOnInventoryRebuilt OnInventoryRebuilt;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void ReadyForReplication() override;

	TArray<FItemSaveData> GetInventorySaveData() const;

	UFUNCTION()
	void OnWebInventoryLoaded(
		const FString& CharacterId, const TArray<FItemSaveData>& LoadedData, bool bSuccess);

	UFUNCTION()
	void OnWebInventorySaved(const FString& CharacterId, bool bSuccess);

private:
	void ConstructInventory();
	void DisplayInventory(bool bShow);
	void RequestPersistentInventory();
	void RestoreInventoryOnAuthority(const TArray<FItemSaveData>& SaveData);
	void MarkInventoryDirty();
	void FlushInventorySave();
	FString GetPersistenceCharacterId() const;

	void HandlePickupOnAuthority(ARPGPickUpBase* ItemPickup);
	void OrganizeInventoryOnAuthority(EItemCategory Category);
	bool IsPickupRequestValid(const ARPGPickUpBase* ItemPickup) const;
	bool IsInventoryItem(const URPGItemBase* Item) const;
	int32 GetMaxStackQuantity(const FItemManifest& Manifest) const;
	int32 FindFreeSlot(EItemCategory Category, int32 PreferredSlot = INDEX_NONE) const;
	URPGItemBase* FindItemAtSlot(
		EItemCategory Category, int32 SlotIndex, const URPGItemBase* IgnoredItem = nullptr) const;
	URPGItemBase* CreateInventoryItem(
		const FItemManifest& Manifest, int32 Quantity, int32 SlotIndex,
		const FGuid& RequestedInstanceId = FGuid());
	void RemoveInventoryItem(URPGItemBase* Item);
	void RejectClientRequest(const FString& Reason);

	UFUNCTION(Client, Reliable)
	void Client_InventoryOperationRejected(const FString& Reason);

	TWeakObjectPtr<ARPGPlayerController> OwningController;

	UPROPERTY(Replicated)
	FInventoryFastArray InventoryList;

	UPROPERTY()
	TObjectPtr<URPGInventoryBase> InventoryMenu;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<URPGInventoryBase> InventoryMenuClass;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnAngleMin = -85.f;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnAngleMax = 85.f;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnDistanceMin = 10.f;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float DropSpawnDistanceMax = 50.f;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	float RelativeSpawnElevation = 70.f;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Authority", meta = (ClampMin = "1"))
	int32 EquipSlotCapacity = 40;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Authority", meta = (ClampMin = "1"))
	int32 ConsumeSlotCapacity = 40;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Authority", meta = (ClampMin = "1"))
	int32 CraftSlotCapacity = 40;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Authority", meta = (ClampMin = "1.0"))
	float MaxPickupDistance = 400.f;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Authority", meta = (ClampMin = "1"))
	int32 MaxQuantityPerRequest = 100000;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Persistence")
	bool bEnableWebPersistence = true;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Persistence", meta = (ClampMin = "0.1"))
	float PersistenceSaveDelay = 1.f;

	bool bShowInventory = false;
	bool bPersistenceLoadFinished = false;
	bool bPersistenceLoadSucceeded = false;
	bool bPersistenceRequestStarted = false;
	bool bInventoryDirty = false;

	FString PersistenceCharacterId;
	FTimerHandle PersistenceSaveTimer;

	TWeakInterfacePtr<IPawnUIInterface> CachedPawnUIInterface;
};
