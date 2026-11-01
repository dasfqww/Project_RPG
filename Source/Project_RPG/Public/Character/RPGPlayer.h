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
class URPGPlayerSkillComponent;
class UQuickSlotComponent;
class UWidgetComponent;

class UPlayerCombatComponent;
class UPlayerUIComponent;
class URPGPlayerWeaponLoadoutComponent;
class URPGPlayerClassComponent;
class URPGPlayerClassDevelopmentComponent;
class URPGCharacterProgressionComponent;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGPlayer : public ARPGBaseCharacter
{
	GENERATED_BODY()
public:
	ARPGPlayer(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ Begin APawn Interface.
	virtual void PossessedBy(AController* NewController) override;
	virtual void PawnClientRestart() override;
	//~ End APawn Interface

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server retry restores presentation only after GAS health/death state is recovered. */
	bool RestoreEncounterPresentation();
	virtual uint32 GetCombatPresentationRevision() const override { return EncounterRecoveryRevision; }

	virtual void Tick(float DeltaTime) override;

	/** Reject queued movement before unpacking a revoked member's network payload. */
	virtual void ProcessEvent(UFunction* Function, void* Parms) override;

	virtual UPawnCombatComponent* GetPawnCombatComponent() const override;

	virtual UPawnUIComponent* GetPawnUIComponent() const override;

	virtual UPlayerUIComponent* GetPlayerUIComponent() const override;

protected:
	//void SetupGASInputComponent();

private:
	TWeakObjectPtr<URPGAbilitySystemComponent> InitializedStartupASC;
	int32 StartupAbilityGrantLevel = 0;
	bool bApplyingStartup = false;
	UFUNCTION()
	void OnRep_EncounterRecoveryRevision();
	void ApplyEncounterRecovery();
	UPROPERTY(ReplicatedUsing = OnRep_EncounterRecoveryRevision)
	uint32 EncounterRecoveryRevision = 0;
	ECollisionEnabled::Type InitialCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	bool bInitialPauseAnims = false;
	bool bPresentationCaptured = false;

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
	TObjectPtr<URPGPlayerWeaponLoadoutComponent> WeaponLoadoutComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
		TObjectPtr<UPlayerUIComponent> PlayerUIComponent;

	/** Shared progression model consumed by the skill UI and GAS skill container. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URPGPlayerSkillComponent> PlayerSkillComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Class", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URPGPlayerClassComponent> PlayerClassComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Progression", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URPGCharacterProgressionComponent> CharacterProgressionComponent;

	/** Inert outside an explicitly opted-in editor-build server process. */
	UPROPERTY(VisibleAnywhere, Category = "Development")
	TObjectPtr<URPGPlayerClassDevelopmentComponent> ClassDevelopmentComponent;

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

	/** Local prediction uses local input; remote server avatars use CMC-validated acceleration. */
	UFUNCTION(BlueprintPure, Category = "RPG|Movement")
	FVector GetAbilityMovementDirection() const;

	void HandleCameraZoom(const FInputActionValue& InputActionValue);

#pragma endregion

	FORCEINLINE UPlayerCombatComponent* GetPlayerCombatComponent() const { return PlayerCombatComponent; }
	FORCEINLINE URPGPlayerSkillComponent* GetPlayerSkillComponent() const { return PlayerSkillComponent; }
	FORCEINLINE URPGSecurityValidationComponent* GetSecurityValidationComponent() const { return SecurityValidationComponent; }
	FORCEINLINE USceneComponent* GetDamageFontComponent() const { return DamageFontComponent; }
	//FORCEINLINE URPGInventoryComponent* GetRPGInventory() const { return PlayerInventory; }
	FORCEINLINE UQuickSlotComponent* GetQuickSlotComponent() const { return QuickSlotComponent; }

};
