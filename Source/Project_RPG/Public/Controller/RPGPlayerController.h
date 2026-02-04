// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Interface/InteractionInterface.h"
#include "RPGPlayerController.generated.h"

USTRUCT()
struct FInteractionData
{
	GENERATED_BODY()
public:
	FInteractionData() :
		CurrentInteractable(nullptr),
		LastInteractionCheckTime(0.f)
	{

	}

	TObjectPtr<AActor> CurrentInteractable;

	float LastInteractionCheckTime;
};

class ARPGHUD;
class ARPGPlayer;
class UDataAsset_InputConfig;
struct FInputActionValue;
class URPGItemBase;
class ARPGPickUpBase;
class URPGInventoryComponent;
class UQuickSlotComponent;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGPlayerController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()
public:
	ARPGPlayerController();

	virtual FGenericTeamId GetGenericTeamId()const;
	
	void UpdateInteractionWidget() const;

	void DropItem(URPGItemBase* ItemToDrop, const int32 QuantityToDrop);

	UFUNCTION(BlueprintCallable, Category = "Input")
	void UpdateInputMappings();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void ApplyKeyMapping(FGameplayTag InTag, FKey NewKey);

	UFUNCTION(BlueprintCallable, Category = "Input")
	FKey GetCurrentKeyForTag(FGameplayTag InTag) const;

protected:
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;

	//void PerformInteractionCheck_LineTrace();
	void PerformInteractionCheck_Around();
	
	void FoundInteractable(AActor* NewInteractable);
	void NoInteractableFound();
	void BeginInteract();
	void EndInteract();
	void Interact();
	
	// �������� ����� �Է� ó��
	void UseQuickSlot(int32 SlotIndex);

	void EnableCameraZoom();

private:
	UPROPERTY()
	TObjectPtr<ARPGHUD> HUD;

	TWeakObjectPtr<URPGInventoryComponent> InventoryComponent;

	UPROPERTY(VisibleAnywhere, Category = "Character|Interaction")
	TScriptInterface<IInteractionInterface> TargetInteractable;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup")
	TSubclassOf<ARPGPickUpBase> PickupClass;

	// ������ ������Ʈ (�÷��̾ �����ϴ� ������)
	UPROPERTY(VisibleAnywhere, Category = "QuickSlot")
	TObjectPtr<UQuickSlotComponent> QuickSlotComponent;

	UPROPERTY(EditDefaultsOnly)
	float PickupCheckRadius = 40.f;

	float InteractionCheckFequency=0.1f;

	float InteractionCheckDistance=225.f;

	FTimerHandle TimerHandle_Interaction;

	FInteractionData InteractionData;

	FTimerHandle ZoomDelayHandle;

	bool bCanZoom=true;

	UPROPERTY()
		ARPGPlayer* PlayerCharacter;

	FGenericTeamId PlayerTeamID;
#pragma region Inputs

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData", meta = (AllowPrivateAccess = "true"))
		UDataAsset_InputConfig* InputConfigDataAsset;

	FVector2D SwitchDirection = FVector2D::ZeroVector;

	void Input_Move(const FInputActionValue& InputActionValue);
	void Input_Look(const FInputActionValue& InputActionValue);

	void Input_CameraZoom(const FInputActionValue& InputActionValue);

	void Input_SwitchTargetTriggered(const FInputActionValue& InputActionValue);
	void Input_SwitchTargetCompleted(const FInputActionValue& InputActionValue);

	void Input_PickUpItemsStarted(const FInputActionValue& InputActionValue);

	void Input_AbilityInputPressed(FGameplayTag InInputTag);
	void Input_AbilityInputReleased(FGameplayTag InInputTag);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ToggleInventory();

	void ShowEquipmentWidget();
#pragma endregion

public:
	FORCEINLINE bool IsInteracting() const { return GetWorldTimerManager().IsTimerActive(TimerHandle_Interaction); };
};
