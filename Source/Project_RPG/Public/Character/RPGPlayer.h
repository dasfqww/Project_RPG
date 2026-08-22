// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/RPGBaseCharacter.h"
#include "RPGPlayer.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UDataAsset_InputConfig;
struct FInputActionValue;
class URPGInventoryComponent;
class URPGSecurityValidationComponent;
class UQuickSlotComponent;
class UWidgetComponent;

class UPlayerCombatComponent;
class UPlayerUIComponent;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGPlayer : public ARPGBaseCharacter
{
	GENERATED_BODY()
public:
	ARPGPlayer();

	//~ Begin APawn Interface.
	virtual void PossessedBy(AController* NewController) override;
	//~ End APawn Interface

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaTime) override;

	virtual UPawnCombatComponent* GetPawnCombatComponent() const override;

	virtual UPawnUIComponent* GetPawnUIComponent() const override;

	virtual UPlayerUIComponent* GetPlayerUIComponent() const override;

protected:
	//void SetupGASInputComponent();

private:
	UPROPERTY(VisibleAnywhere, Category = "UI", meta = (AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> DamageFontComponent;

#pragma region CameraZoom

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float CameraZoomMin = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float CameraZoomMax = 400.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float CameraZoomStep = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float CameraZoomLerpSpeed = 10.f;

	float TargetArmLength;

#pragma region Components

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
		TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
		TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
		TObjectPtr<UPlayerCombatComponent> PlayerCombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
		TObjectPtr<UPlayerUIComponent> PlayerUIComponent;

	/** Server-owned movement and combat anomaly monitor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Security", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URPGSecurityValidationComponent> SecurityValidationComponent;

	/*UPROPERTY(VisibleAnywhere, Category = "Character | Inventory")
		TObjectPtr<URPGInventoryComponent> PlayerInventory;*/
		
	UPROPERTY(VisibleAnywhere, Category = "Character | QuickSlot")
	TObjectPtr<UQuickSlotComponent> QuickSlotComponent;
#pragma endregion

#pragma region Movements

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData", meta = (AllowPrivateAccess = "true"))
		TObjectPtr<UDataAsset_InputConfig> InputConfigDataAsset;

public:
	void Move(const FInputActionValue& InputActionValue);
	void Look(const FInputActionValue& InputActionValue);

	void HandleCameraZoom(const FInputActionValue& InputActionValue);

#pragma endregion

	FORCEINLINE UPlayerCombatComponent* GetPlayerCombatComponent() const { return PlayerCombatComponent; }
	FORCEINLINE URPGSecurityValidationComponent* GetSecurityValidationComponent() const { return SecurityValidationComponent; }
	FORCEINLINE USceneComponent* GetDamageFontComponent() const { return DamageFontComponent; }
	//FORCEINLINE URPGInventoryComponent* GetRPGInventory() const { return PlayerInventory; }
	FORCEINLINE UQuickSlotComponent* GetQuickSlotComponent() const { return QuickSlotComponent; }

};
