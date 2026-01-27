// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGBossBattleInfoWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGBossBattleInfoWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:

	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable)
	void UpdateBattleLimitTimeText(UTextBlock* TextBlock, float InRemainingTime);

	UFUNCTION(BlueprintCallable)
	void OnRestart();

	UFUNCTION(BlueprintCallable)
	void OnExit();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Battle")
	TArray<UImage*> LifeImages;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> TargetImage;

	/*UPROPERTY(VisibleAnywhere,BlueprintReadOnly,meta = (BindWidget))
	TObjectPtr<UTextBlock> BossNameText;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> RemainTimeText;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> DeathCountText;*/


};
