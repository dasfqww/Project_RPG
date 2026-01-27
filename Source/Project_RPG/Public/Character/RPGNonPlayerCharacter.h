// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Character/RPGBaseCharacter.h"
#include "RPGNonPlayerCharacter.generated.h"

class UNPCCombatComponent;
class UNPCUIComponent;
class UWidgetComponent;
class UBoxComponent;



/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGNonPlayerCharacter : public ARPGBaseCharacter
{
	GENERATED_BODY()
public:
	ARPGNonPlayerCharacter();

	virtual UPawnCombatComponent* GetPawnCombatComponent() const override;

	virtual UPawnUIComponent* GetPawnUIComponent() const override;

	virtual UNPCUIComponent* GetNPCUIComponent()const override;

	

protected:
	virtual void BeginPlay() override;

	//~ Begin APawn Interface.
	virtual void PossessedBy(AController* NewController) override;
	//~ End APawn Interface

#if WITH_EDITOR
//~ Begin UObject Interface.
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif
	

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
		TObjectPtr<UNPCCombatComponent> NPCCombatComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
		TObjectPtr<UBoxComponent> LeftHandCollisionBox;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
		FName LeftHandCollisionBoxAttachBoneName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UBoxComponent> RightHandCollisionBox;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
		FName RightHandCollisionBoxAttachBoneName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UBoxComponent> BodyCollisionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
		TObjectPtr<UNPCUIComponent> NPCUIComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UWidgetComponent> NPCHealthWidgetComponent;

	UFUNCTION()
		virtual void OnBodyCollisionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, 
			AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
			bool bFromSweep, const FHitResult& SweepResult);

private:
	void InitNPCStartUpData();

public:
	FORCEINLINE UNPCCombatComponent* GetNPCCombatComponent() const { return NPCCombatComponent; }
	FORCEINLINE UBoxComponent* GetLeftHandCollisionBox() const { return LeftHandCollisionBox; }
	FORCEINLINE UBoxComponent* GetRightHandCollisionBox() const { return RightHandCollisionBox; }
};
