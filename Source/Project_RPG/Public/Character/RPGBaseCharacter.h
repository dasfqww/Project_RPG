// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Interface/PawnCombatInterface.h"
#include "Interface/PawnUIInterface.h"
#include "RPGBaseCharacter.generated.h"

class URPGAbilitySystemComponent;
class URPGAttributeSet;
class URPGEquipComponent;
class URPGEquipmentComponent;
class UDataAsset_StartUpDataBase;
class UMotionWarpingComponent;
class UDamageFontWidget;

UCLASS()
class PROJECT_RPG_API ARPGBaseCharacter : public ACharacter, 
	public IAbilitySystemInterface, public IPawnCombatInterface, public IPawnUIInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ARPGBaseCharacter();

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaTime) override;

	//~ Begin IAbilitySystemInterface Interface.
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const;
	//~ End IAbilitySystemInterface Interface

	virtual UPawnCombatComponent* GetPawnCombatComponent() const override;

	virtual UPawnUIComponent* GetPawnUIComponent() const override;

	virtual UPlayerUIComponent* GetPlayerUIComponent()const override;

	virtual UNPCUIComponent* GetNPCUIComponent()const override;

	//~ Begin APawn Interface.
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	//~ End APawn Interface

	void ShowDamageFont(float Damage, FVector Location, bool bCriticalAttack, bool bIsPlayerDamage);

	void ShowInvincibleFont(FVector Location);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
		TObjectPtr<URPGAbilitySystemComponent> RPGAbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AbilitySystem")
		TObjectPtr<URPGAttributeSet> RPGAttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
		TObjectPtr<URPGEquipComponent> EquipComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
		TObjectPtr<URPGEquipmentComponent> EquipmentComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionWarping")
		TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData")
		TSoftObjectPtr<UDataAsset_StartUpDataBase> CharacterStartUpData;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effect", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> NS_SceneComponent;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effect", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UDamageFontWidget> DamageFontWidgetClass;

	/*UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effect", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UGameplayEffect> HealingEffect;*/

	FTimerHandle DamageFontUpdateTimerHandle;

public:
	FORCEINLINE URPGAbilitySystemComponent* GetRPGAbilitySystemComponent() const { return RPGAbilitySystemComponent; }
	FORCEINLINE URPGAttributeSet* GetRPGAttributeSet() const { return RPGAttributeSet; }
	//FORCEINLINE TSubclassOf<UGameplayEffect> GetHealingEffect() const { return HealingEffect; }
	FORCEINLINE USceneComponent* GetNS_SceneComponent() const { return NS_SceneComponent; }
};
