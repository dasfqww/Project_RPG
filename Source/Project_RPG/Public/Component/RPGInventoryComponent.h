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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryItemChanged, URPGItemBase*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnQuantityChanged, const FSlotAvailabilityResult&, Result);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent), Blueprintable)
class PROJECT_RPG_API URPGInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	URPGInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	FOnInventoryUpdated OnInventoryUpdated;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void TryAddItem(ARPGPickUpBase* InPickup);

	UFUNCTION(Server, Reliable)
	void Server_AddNewItem(ARPGPickUpBase* ItemPickup, int32 Quantity, int32 TargetSlotIndex = -1);

	UFUNCTION(Server, Reliable)
	void Server_RestoreInventory(const TArray<FItemSaveData>& SaveData);

	UFUNCTION(Server, Reliable)
	void Server_AddStacksToItem(ARPGPickUpBase* ItemPickup, int32 Quantity, int32 Remainder);

	UFUNCTION(Server, Reliable)
	void Server_DropItem(URPGItemBase* Item, int32 Quantity);

	UFUNCTION(Server, Reliable)
	void Server_ConsumeItem(URPGItemBase* Item);

	UFUNCTION(Server, Reliable)
	void Server_MoveItem(URPGItemBase* Item, int32 NewSlotIndex);

	void ToggleInventoryMenu();
	void AddRepSubObj(UObject* SubObj);
	void SpawnDroppedItem(URPGItemBase* Item, int32 Quantity);
	
	FORCEINLINE URPGInventoryBase* GetInventoryMenu() const { return InventoryMenu; }

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	TArray<URPGItemBase*> GetAllItems() const;

	FOnInventoryItemChanged OnItemAdded; 
	FOnInventoryItemChanged OnItemRemoved;
	FOnQuantityChanged OnQuantityChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	TArray<FItemSaveData> GetInventorySaveData();
	void RestoreInventoryFromData(const TArray<FItemSaveData>& SaveData);

	UFUNCTION()
	void OnWebInventoryLoaded(const TArray<FItemSaveData>& LoadedData);

private:
	void ConstructInventory();
	void DisplayInventory(bool bShow);

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

	bool bShowInventory = false;

	TWeakInterfacePtr<IPawnUIInterface> CachedPawnUIInterface;

public:
};