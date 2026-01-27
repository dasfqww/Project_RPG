// Fill out your copyright notice in the Description page of Project Settings.


#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "RPGMessageBox.generated.h"

class UTextBlock;
class USoundBase;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGMessageBox : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable)
	void OnConfirm();
	
	UFUNCTION(BlueprintCallable)
	void OnCancel();

	

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Message")
	FText Message;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MessageText;

	FGameplayTag GameLevelTag;

private:
	UPROPERTY(EditDefaultsOnly, Category = "Sound")
	TObjectPtr<USoundBase> PopUpSound;

public:
	FORCEINLINE void SetGameLevelTag(FGameplayTag LevelTag) { GameLevelTag = LevelTag; }
};