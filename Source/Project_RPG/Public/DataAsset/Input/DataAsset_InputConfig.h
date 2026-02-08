// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DataAsset_InputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

USTRUCT(BlueprintType)
struct FWarriorInputActionConfig
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "InputTag"))
		FGameplayTag InputTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UInputAction> InputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText InputActionName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bShowInInputOptionMenu = true;

	bool IsValid() const{
		return InputTag.IsValid() && InputAction;
	}
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UDataAsset_InputConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
		UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (TitleProperty = "InputTag"))
		TArray<FWarriorInputActionConfig> NativeInputActions;

	UInputAction* FindNativeInputActionByTag(const FGameplayTag& InInputTag) const;
	UInputAction* FindAbilityInputActionByTag(const FGameplayTag& InInputTag) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (TitleProperty = "InputTag"))
		TArray<FWarriorInputActionConfig> AbilityInputActions;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
