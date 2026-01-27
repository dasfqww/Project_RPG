// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "Type/RPGEnumTypes.h"
#include "GameplayTagContainer.h"
#include "RPGContentEntryWidget.generated.h"

class UTextBlock;
class UImage;
class UWidgetSwitcher;
class UWrapBox;
class URPGRewardItemWidget;
class ARPGGameModeBase;
class URPGMessageBox;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGContentEntryWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	URPGContentEntryWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable)
	void OnDifficultyButtonClicked(ERPGGameDifficulty InGameDifficulty);

	//void UpdateRewardUIByDifficulty(ERPGGameDifficulty InGameDifficulty);



	//void CreateRewardItemReference();

	//void CreateAndAddRewardItemWidget(const FRPGItemData& ItemData, int32 Quantity);

protected:
	
	UFUNCTION(BlueprintCallable)
	void OnEntryButtonClicked();

	UFUNCTION(BlueprintCallable)
	void OnCloseButtonClicked();

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Difficulty")
	ERPGGameDifficulty GameDifficulty;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ContentNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ContentImage;
	
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWrapBox> RewardInfoPanel;

	UPROPERTY(EditDefaultsOnly, Category = "Data Table")
	TObjectPtr<UDataTable> RewardDataTable;
	
	UPROPERTY(EditDefaultsOnly, Category = "Data Table")
	TObjectPtr<UDataTable> ItemDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "Item")
	TSubclassOf<URPGRewardItemWidget> RewardItemWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "GameMode")
	TSubclassOf<ARPGGameModeBase> GameMode;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<URPGMessageBox> MessageBoxClass;

	UPROPERTY(EditDefaultsOnly, Category = "Level Tag")
	FGameplayTag LevelTag;
};
