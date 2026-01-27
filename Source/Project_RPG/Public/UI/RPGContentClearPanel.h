// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGContentClearPanel.generated.h"

class UWrapBox;
class ARPGGameModeBase;
class URPGItemBase;
class URPGRewardItemWidget;
class USizeBox;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGContentClearPanel : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	URPGContentClearPanel();

	virtual void NativeConstruct() override;

	//virtual void NativeOnInitialized() override;

	void ShowContentClearReward();

	//void SetPanelSize(float Width, float Height);

protected:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> RewardInfoPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> ClearPanelSizeBox;

	UPROPERTY()
	TObjectPtr<ARPGGameModeBase> GameModeReference;



	UPROPERTY(EditDefaultsOnly, Category = "Reward")
	TSubclassOf<URPGRewardItemWidget> RewardItemWidgetClass;
};
