// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractionInterface.h"
#include "Item/Manifest/RPGItemManifest.h"
#include "RPGPickUpBase.generated.h"

class USphereComponent;
class ARPGPlayer;
class URPGItemBase;
class URPGItemNameWidget;
class UWidgetComponent;

UCLASS()
class PROJECT_RPG_API ARPGPickUpBase : public AActor, public IInteractionInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ARPGPickUpBase();

	virtual void BeginPlay() override;
	
	

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitItemManifest(FItemManifest CopyOfManifest, FName InPoolName = NAME_None);

	//void InitializePickUp(const TSubclassOf<URPGItemBase> BaseClass, const int32 InQuantity);

	void InitializeDrop(URPGItemBase* ItemToDrop, const int32 InQuantity);

	void PickedUp();
	bool TryClaimPickup();
	void ReleasePickupClaim();
	void SetPickupQuantity(int32 Quantity);
	bool IsAvailableForPickup() const { return bIsAvailableForPickup; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Pickup | Components")
	TObjectPtr<UStaticMeshComponent> PickupMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup | Item DataBase")
	TObjectPtr<UDataTable> ItemDataTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup | Item DataBase")
	FName DesiredItemID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup | Item Reference")
	TObjectPtr<URPGItemBase> ItemReference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup | Item Initialization")
	int32 ItemQuantity;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup | Interaction")
	FInteractableData InstanceInteractableData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	UWidgetComponent* ItemTextWidgetComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pick UP Interaction")
	TObjectPtr<USphereComponent> PickUpCollisionSphere;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup | Interaction")
	TSubclassOf<URPGItemNameWidget> ItemNameWidgetClass;
	
	UPROPERTY(Replicated, EditAnywhere, Category = "Inventory")
	FItemManifest ItemManifest;

	UPROPERTY(ReplicatedUsing = OnRep_PickupAvailability)
	bool bIsAvailableForPickup = true;

	UFUNCTION()
	void OnRep_PickupAvailability();

	UFUNCTION()
	/*virtual void OnPickUpCollisionSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, 
		AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, 
		const FHitResult& SweepResult);*/

	virtual void BeginFocus() override;
	virtual void EndFocus() override;
	virtual void BeginInteract() override;
	virtual void EndInteract() override;
	virtual void Interact(APlayerController* PlayerController) override;

	void UpdateInteractableData();

	void TakePickup(APlayerController* PlayerController);

	FName OwningPoolName = NAME_None;

	/*UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void OnPickedUp();*/

	

#if WITH_EDITOR
	//virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	FORCEINLINE URPGItemBase* GetRPGItemData() const { return ItemReference; }
	FORCEINLINE UWidgetComponent* GetWidgetComponent() const { return ItemTextWidgetComponent; }
	FORCEINLINE FItemManifest GetItemManifest() const { return ItemManifest; }
};
