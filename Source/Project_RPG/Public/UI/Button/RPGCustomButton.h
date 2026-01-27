// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "RPGCustomButton.generated.h"

class USoundBase;
class USoundManager;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGCustomButton : public UButton
{
	GENERATED_BODY()
public:
	URPGCustomButton();

protected:
	virtual void SynchronizeProperties() override;
	virtual void PostLoad() override;

	// 클릭 or 호버 등에서 사용할 사운드
	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> HoverSound;

	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> PressedSound;

	// 커스텀 바인딩
	UFUNCTION(BlueprintCallable)
	void OnHoveredCallback();

	UFUNCTION(BlueprintCallable)
	void OnPressedCallback();
};
